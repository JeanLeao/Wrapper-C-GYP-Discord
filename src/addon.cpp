#include <napi.h>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#define DISCORDPP_IMPLEMENTATION
#include "discordpp.h"

std::shared_ptr<discordpp::Client> client;
std::unique_ptr<discordpp::Call> activeCall;
uint64_t applicationId = 0;
std::atomic<int32_t> currentClientStatus{0};
std::atomic<uint64_t> activeLobbyId{0};
std::atomic<uint64_t> capturedCallbackCount{0};
std::atomic<uint64_t> receivedCallbackCount{0};
std::atomic<float> lastCapturedDbfs{-100.0f};
std::atomic<float> lastReceivedDbfs{-100.0f};
std::atomic<int32_t> lastCapturedSampleRate{0};
std::atomic<uint64_t> lastCapturedChannels{0};

float CalculateDbfs(const int16_t* data, uint64_t sampleCount) {
  if (!data || sampleCount == 0) {
    return -100.0f;
  }

  double sumSquares = 0.0;
  for (uint64_t i = 0; i < sampleCount; ++i) {
    const double sample = static_cast<double>(data[i]) / 32768.0;
    sumSquares += sample * sample;
  }

  const double rms = std::sqrt(sumSquares / static_cast<double>(sampleCount));
  if (rms <= 0.00001) {
    return -100.0f;
  }

  return static_cast<float>(20.0 * std::log10(rms));
}

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

  if (std::getenv("VERBOSE_VOICE_LOGS")) {
    client->AddVoiceLogCallback([](auto message, auto severity) {
      printf("[Discord Voice] %s\n", message.c_str());
      fflush(stdout);
    }, discordpp::LoggingSeverity::Info);
  }

  client->SetVoiceParticipantChangedCallback([](uint64_t lobbyId,
                                                uint64_t userId,
                                                bool added) {
    printf("[Voice] Lobby %llu | usuario %llu %s da call\n",
           lobbyId,
           userId,
           added ? "entrou" : "saiu");
    fflush(stdout);
  });

  client->SetNoAudioInputThreshold(-60.0f);
  client->SetNoAudioInputCallback([](bool inputDetected) {
    printf("[Voice] Microfone %s audio acima do limite\n",
           inputDetected ? "recebendo" : "sem");
    fflush(stdout);
  });

  client->SetStatusChangedCallback([](auto status, auto error, auto errorDetail) {
    currentClientStatus.store(static_cast<int32_t>(status));

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

void AttachCallCallbacks(discordpp::Call& call) {
  call.SetStatusChangedCallback([](discordpp::Call::Status status,
                                   discordpp::Call::Error error,
                                   int32_t errorDetail) {
    printf("[Voice] Status da call: %s\n",
           discordpp::Call::StatusToString(status).c_str());

    if (error != discordpp::Call::Error::None) {
      printf("[Voice] Erro da call: %s (%d)\n",
             discordpp::Call::ErrorToString(error).c_str(),
             errorDetail);
    }

    fflush(stdout);
  });

  call.SetParticipantChangedCallback([](uint64_t userId, bool added) {
    printf("[Voice] Participante %llu %s\n", userId, added ? "entrou" : "saiu");
    fflush(stdout);
  });

  call.SetSpeakingStatusChangedCallback([](uint64_t userId, bool isPlayingSound) {
    printf("[Voice] Usuario %llu %s\n",
           userId,
           isPlayingSound ? "falando" : "parou de falar");
    fflush(stdout);
  });

  call.SetSelfMute(false);
  call.SetSelfDeaf(false);
}

Napi::Value StartLobbyVoice(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!client) {
    Napi::Error::New(env, "Client nao inicializado").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "lobbySecret e obrigatorio").ThrowAsJavaScriptException();
    return env.Null();
  }

  const std::string lobbySecret = info[0].As<Napi::String>();
  if (lobbySecret.empty()) {
    Napi::TypeError::New(env, "lobbySecret nao pode ser vazio").ThrowAsJavaScriptException();
    return env.Null();
  }

  client->SetInputVolume(100.0f);
  client->SetOutputVolume(100.0f);
  client->SetNoiseSuppression(true);
  client->SetEchoCancellation(true);
  client->SetAutomaticGainControl(true);

  printf("[Lobby] Criando/entrando no lobby com secret: %s\n", lobbySecret.c_str());
  fflush(stdout);

  client->CreateOrJoinLobby(
    lobbySecret,
    [](const discordpp::ClientResult& result, uint64_t lobbyId) {
      if (!result.Successful()) {
        printf("[Lobby] Erro ao criar/entrar no lobby: %s\n", result.Error().c_str());
        fflush(stdout);
        return;
      }

      activeLobbyId.store(lobbyId);
      capturedCallbackCount.store(0);
      receivedCallbackCount.store(0);
      lastCapturedDbfs.store(-100.0f);
      lastReceivedDbfs.store(-100.0f);

      printf("[Lobby] Lobby pronto: %llu\n", lobbyId);
      printf("[Voice] Iniciando call com callbacks de audio...\n");
      fflush(stdout);

      auto call = client->StartCallWithAudioCallbacks(
        lobbyId,
        [](uint64_t userId,
           int16_t* data,
           uint64_t samplesPerChannel,
           int32_t sampleRate,
           uint64_t channels,
           bool& outShouldMute) {
          outShouldMute = false;
          const uint64_t callbackCount = receivedCallbackCount.fetch_add(1) + 1;

          const uint64_t totalSamples = samplesPerChannel * channels;
          const float dbfs = CalculateDbfs(data, totalSamples);
          lastReceivedDbfs.store(dbfs);

          if (callbackCount % 100 == 1) {
            printf("[Voice][RX] user=%llu rate=%d canais=%llu amostras=%llu nivel=%.1f dBFS\n",
                   userId,
                   sampleRate,
                   channels,
                   samplesPerChannel,
                   dbfs);
            fflush(stdout);
          }
        },
        [](int16_t* data,
           uint64_t samplesPerChannel,
           int32_t sampleRate,
           uint64_t channels) {
          const uint64_t callbackCount = capturedCallbackCount.fetch_add(1) + 1;
          lastCapturedSampleRate.store(sampleRate);
          lastCapturedChannels.store(channels);

          const uint64_t totalSamples = samplesPerChannel * channels;
          const float dbfs = CalculateDbfs(data, totalSamples);
          lastCapturedDbfs.store(dbfs);

          if (callbackCount % 100 == 1) {
            printf("[Voice][MIC] rate=%d canais=%llu amostras=%llu nivel=%.1f dBFS\n",
                   sampleRate,
                   channels,
                   samplesPerChannel,
                   dbfs);
            fflush(stdout);
          }
        });

      if (!call) {
        call = client->GetCall(lobbyId);
        if (!call) {
          printf("[Voice] Ja estava na call, mas nao consegui recuperar o handle.\n");
          fflush(stdout);
          return;
        }

        printf("[Voice] Usuario ja estava nessa call; handle recuperado.\n");
      }

      AttachCallCallbacks(call);
      activeCall = std::make_unique<discordpp::Call>(std::move(call));

      printf("[Voice] Call iniciada/entrada solicitada para lobby %llu.\n", lobbyId);
      fflush(stdout);
    });

  return Napi::Boolean::New(env, true);
}

Napi::Value GetVoiceStats(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::Object stats = Napi::Object::New(env);

  stats.Set("lobbyId", Napi::String::New(env, std::to_string(activeLobbyId.load())));
  stats.Set("hasActiveCall", Napi::Boolean::New(env, activeCall && *activeCall));
  stats.Set("capturedCallbacks", Napi::Number::New(env, capturedCallbackCount.load()));
  stats.Set("receivedCallbacks", Napi::Number::New(env, receivedCallbackCount.load()));
  stats.Set("lastCapturedDbfs", Napi::Number::New(env, lastCapturedDbfs.load()));
  stats.Set("lastReceivedDbfs", Napi::Number::New(env, lastReceivedDbfs.load()));
  stats.Set("lastCapturedSampleRate", Napi::Number::New(env, lastCapturedSampleRate.load()));
  stats.Set("lastCapturedChannels", Napi::Number::New(env, lastCapturedChannels.load()));

  if (activeCall && *activeCall) {
    stats.Set("callStatus", Napi::String::New(
      env,
      discordpp::Call::StatusToString(activeCall->GetStatus())));
    stats.Set("selfMute", Napi::Boolean::New(env, activeCall->GetSelfMute()));
    stats.Set("selfDeaf", Napi::Boolean::New(env, activeCall->GetSelfDeaf()));
  }

  return stats;
}

Napi::Value GetClientStatus(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  const auto status = static_cast<discordpp::Client::Status>(currentClientStatus.load());

  return Napi::String::New(env, discordpp::Client::StatusToString(status));
}

Napi::Value SetSelfMute(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!activeCall || !*activeCall) {
    Napi::Error::New(env, "Nenhuma call ativa").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (info.Length() < 1 || !info[0].IsBoolean()) {
    Napi::TypeError::New(env, "mute booleano e obrigatorio").ThrowAsJavaScriptException();
    return env.Null();
  }

  const bool mute = info[0].As<Napi::Boolean>();
  activeCall->SetSelfMute(mute);
  return Napi::Boolean::New(env, true);
}

Napi::Value EndLobbyVoice(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!client || activeLobbyId.load() == 0) {
    return Napi::Boolean::New(env, false);
  }

  const uint64_t lobbyId = activeLobbyId.load();
  client->EndCall(lobbyId, [lobbyId]() {
    printf("[Voice] Call encerrada para lobby %llu\n", lobbyId);
    fflush(stdout);
  });

  activeCall.reset();
  activeLobbyId.store(0);

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
    const uint64_t lobbyId = activeLobbyId.load();
    if (lobbyId != 0) {
      client->EndCall(lobbyId, []() {
        printf("[Voice] Call encerrada no destroyClient\n");
        fflush(stdout);
      });
    }
    activeCall.reset();
    activeLobbyId.store(0);
    client->Disconnect();
    client.reset();
  }

  return Napi::Boolean::New(env, true);
}

Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
  exports.Set("initClient", Napi::Function::New(env, InitClient));
  exports.Set("authorize", Napi::Function::New(env, Authorize));
  exports.Set("sendMessage", Napi::Function::New(env, SendMessage));
  exports.Set("startLobbyVoice", Napi::Function::New(env, StartLobbyVoice));
  exports.Set("getClientStatus", Napi::Function::New(env, GetClientStatus));
  exports.Set("getVoiceStats", Napi::Function::New(env, GetVoiceStats));
  exports.Set("setSelfMute", Napi::Function::New(env, SetSelfMute));
  exports.Set("endLobbyVoice", Napi::Function::New(env, EndLobbyVoice));
  exports.Set("runCallbacks", Napi::Function::New(env, RunCallbacks));
  exports.Set("destroyClient", Napi::Function::New(env, DestroyClient));
  return exports;
}

NODE_API_MODULE(discord_social, InitModule)
