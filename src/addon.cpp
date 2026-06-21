#include <napi.h>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

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

  std::string applicationIdString = info[0].As<Napi::String>();
  applicationId = ParseApplicationId(applicationIdString);

  if (applicationId == 0) {
    Napi::TypeError::New(env, "applicationId invalido").ThrowAsJavaScriptException();
    return env.Null();
  }

  client = std::make_shared<discordpp::Client>();
  client->SetApplicationId(applicationId);

  client->AddLogCallback([](auto message, auto severity) {
    printf("[Discord SDK] %s\n", message.c_str());
    fflush(stdout);
  }, discordpp::LoggingSeverity::Info);

  client->SetStatusChangedCallback([](auto status, auto error, auto errorDetail) {
    printf("[Discord SDK] Status: %s\n", discordpp::Client::StatusToString(status).c_str());

    if (status == discordpp::Client::Status::Ready && client) {
      printf("[Discord SDK] Client pronto!\n");
      printf("[Discord SDK] Amigos: %zu\n", client->GetRelationships().size());
    }

    if (error != discordpp::Client::Error::None) {
      printf("[Discord SDK] Erro: %s (%d)\n",
             discordpp::Client::ErrorToString(error).c_str(),
             errorDetail);
    }

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

    client->GetToken(applicationId, code, codeVerifier.Verifier(), redirectUri,
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
      });
  });

  return Napi::Boolean::New(env, true);
}

Napi::Value RunCallbacks(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  discordpp::RunCallbacks();

  return Napi::Boolean::New(env, true);
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
  printf("teste\n");
  fflush(stdout);

  exports.Set("initClient", Napi::Function::New(env, InitClient));
  exports.Set("authorize", Napi::Function::New(env, Authorize));
  exports.Set("runCallbacks", Napi::Function::New(env, RunCallbacks));
  exports.Set("destroyClient", Napi::Function::New(env, DestroyClient));
  return exports;
}

NODE_API_MODULE(discord_social, InitModule)
