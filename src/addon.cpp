#include <napi.h>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#define DISCORDPP_IMPLEMENTATION
#include "discordpp.h"

std::shared_ptr<discordpp::Client> client;
uint64_t applicationId = 0;

uint64_t ParseApplicationId(const std::string& value) {
  char* end = nullptr;
  unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);

  if (end == value.c_str() || *end != '\0') {
    return 0;
  }

  return static_cast<uint64_t>(parsed);
}

Napi::Value InitClient(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "applicationId é obrigatório").ThrowAsJavaScriptException();
    return env.Null();
  }

  applicationId = ParseApplicationId(info[0].As<Napi::String>());

  if (applicationId == 0) {
    Napi::TypeError::New(env, "applicationId inválido").ThrowAsJavaScriptException();
    return env.Null();
  }

  client = std::make_shared<discordpp::Client>();
  client->SetApplicationId(applicationId);

  client->AddLogCallback([](auto message, auto severity) {
    printf("[Discord SDK] %s\n", message.c_str());
    fflush(stdout);
  }, discordpp::LoggingSeverity::Info);

  client->SetStatusChangedCallback([](auto status, auto error, auto errorDetail) {
    printf("[Discord SDK] Status: %s\n",
           discordpp::Client::StatusToString(status).c_str());

    if (error != discordpp::Client::Error::None) {
      printf("[Discord SDK] Erro: %s (%d)\n",
             discordpp::Client::ErrorToString(error).c_str(),
             errorDetail);
    }

    if (status != discordpp::Client::Status::Ready || !client) {
      fflush(stdout);
      return;
    }

    printf("[Discord SDK] Client pronto!\n");
    printf("[Discord SDK] Amigos: %zu\n", client->GetRelationships().size());

    auto user = client->GetCurrentUser();

    printf("[Discord SDK] Usuario atual: %s\n", user.Username().c_str());

    printf("[Discord SDK] Avatar: %s\n",
           user.AvatarUrl(
             discordpp::UserHandle::AvatarType::Png,
             discordpp::UserHandle::AvatarType::Png
           ).c_str());

    discordpp::Activity activity{};
    activity.SetType(discordpp::ActivityTypes::Playing);
    activity.SetName("Minha Plataforma");
    activity.SetDetails("ready to queue?");

    client->UpdateRichPresence(activity, [](auto result) {
      if (!result.Successful()) {
        printf("[Discord SDK] Erro ao atualizar activity: %s\n",
               result.Error().c_str());
        fflush(stdout);
        return;
      }

      printf("[Discord SDK] Activity atualizada!\n");
      fflush(stdout);
    });

    fflush(stdout);
  });

  return Napi::Boolean::New(env, true);
}

Napi::Value Authorize(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!client || applicationId == 0) {
    Napi::Error::New(env, "chame initClient(applicationId) antes de authorize()")
      .ThrowAsJavaScriptException();
    return env.Null();
  }

  auto codeVerifier = client->CreateAuthorizationCodeVerifier();

  discordpp::AuthorizationArgs args{};
  args.SetClientId(applicationId);
  args.SetScopes(discordpp::Client::GetDefaultCommunicationScopes());
  args.SetScopes(discordpp::Client::GetDefaultPresenceScopes());
  args.SetCodeChallenge(codeVerifier.Challenge());

  printf("[OAuth] Abrindo autorizacao do Discord...\n");
  fflush(stdout);

  client->Authorize(args, [codeVerifier](auto result, auto code, auto redirectUri) {
    if (!result.Successful()) {
      printf("[OAuth] Erro na autorizacao: %s\n", result.Error().c_str());
      fflush(stdout);
      return;
    }

    printf("[OAuth] Autorizado. Trocando code por access token...\n");
    fflush(stdout);

    client->GetToken(
      applicationId,
      code,
      codeVerifier.Verifier(),
      redirectUri,
      [](discordpp::ClientResult result,
         std::string accessToken,
         std::string refreshToken,
         discordpp::AuthorizationTokenType tokenType,
         int32_t expiresIn,
         std::string scopes) {
        if (!result.Successful()) {
          printf("[OAuth] Erro ao buscar token: %s\n", result.Error().c_str());
          fflush(stdout);
          return;
        }

        printf("[OAuth] Token recebido. Expira em %d segundos. Scopes: %s\n",
               expiresIn,
               scopes.c_str());
        fflush(stdout);

        client->UpdateToken(tokenType, accessToken, [](discordpp::ClientResult updateResult) {
          if (!updateResult.Successful()) {
            printf("[OAuth] Erro no UpdateToken: %s\n", updateResult.Error().c_str());
            fflush(stdout);
            return;
          }

          printf("[OAuth] Token atualizado. Conectando ao Discord...\n");
          fflush(stdout);

          client->Connect();
        });
      }
    );
  });

  return Napi::Boolean::New(env, true);
}

Napi::Value SendMessage(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!client) {
    Napi::Error::New(env, "Client não inicializado").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::string friendName = "";
  std::string message = "";

  if (info.Length() >= 1 && info[0].IsString()) {
    friendName = info[0].As<Napi::String>();
  }
  if (info.Length() >= 2 && info[1].IsString()) {
    message = info[1].As<Napi::String>();
  }

  auto myFriend = discordpp::RelationshipHandle::nullobj;

  for (const auto& relationship : client->GetRelationships()) {
    auto user = relationship.User();

    if (!user) {
      continue;
    }

    printf("[Discord SDK] Amigo encontrado: %s | ID: %llu\n",
           user->DisplayName().c_str(),
           user->Id());

    if (user->DisplayName() == friendName) {
      myFriend = relationship;
      break;
    }
  }

  if (myFriend == discordpp::RelationshipHandle::nullobj) {
    Napi::Error::New(env, "Amigo não encontrado").ThrowAsJavaScriptException();
    return env.Null();
  }

  client->SendUserMessage(
    myFriend.User()->Id(),
    message,
    [](auto result, uint64_t messageId) {
      if (!result.Successful()) {
        printf("[Discord SDK] Erro ao enviar mensagem: %s\n",
               result.Error().c_str());
        fflush(stdout);
        return;
      }

      printf("[Discord SDK] Mensagem enviada com ID: %llu\n", messageId);
      fflush(stdout);
    }
  );

  return Napi::Boolean::New(env, true);
}

Napi::Value RunCallbacks(const Napi::CallbackInfo& info) {
  discordpp::RunCallbacks();
  return Napi::Boolean::New(info.Env(), true);
}

Napi::Value DestroyClient(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (client) {
    client->Disconnect();
    client.reset();
  }

  return Napi::Boolean::New(env, true);
}

Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
  exports.Set("initClient", Napi::Function::New(env, InitClient));
  exports.Set("authorize", Napi::Function::New(env, Authorize));
  exports.Set("sendMessage", Napi::Function::New(env, SendMessage));
  exports.Set("runCallbacks", Napi::Function::New(env, RunCallbacks));
  exports.Set("destroyClient", Napi::Function::New(env, DestroyClient));
  return exports;
}

NODE_API_MODULE(discord_social, InitModule)