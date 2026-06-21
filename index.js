const path = require("node:path");
const sdkBinPath = path.join(__dirname, "lib", "discord_social_sdk", "bin", "release");
process.env.PATH = `${sdkBinPath}${path.delimiter}${process.env.PATH || ""}`;
const discord = require("./build/Release/discord_social.node");
const env = require("dotenv").config();
if (process.argv.includes("--load-only")) {
  console.log("Binding carregado:", Object.keys(discord));
  process.exit(0);
}
const applicationId = process.env.DISCORD_APPLICATION_ID;
if (!applicationId) {
  console.error("Uso: defina DISCORD_APPLICATION_ID no ambiente.");
  console.error("Opcional: defina LOBBY_SECRET para criar/entrar no lobby de voz.");
  process.exit(1);
}
const lobbySecret = "discord-social-wrapper-dev-lobby"

console.log("Exports do binding:", Object.keys(discord));
console.log("initClient:", discord.initClient(String(applicationId)));
discord.onVoiceEvent((event) => {
  console.log("voiceEvent:", event);
});
console.log("authorize:", discord.authorize());

let shuttingDown = false;

const interval = setInterval(() => {
  try {
    discord.runCallbacks();
  } catch (error) {
    console.error(error);
    shutdown(1);
  }
}, 10);

const timeouts = [];

function shutdown(code = 0) {
  if (shuttingDown) {
    process.reallyExit(code);
    return;
  }

  shuttingDown = true;
  clearInterval(interval);
  for (const timeout of timeouts) clearTimeout(timeout);
  try {
    if (lobbySecret) {
      discord.endLobbyVoice();
    }
    discord.destroyClient();
  } catch (error) {
    console.error(error);
  }
  console.log("\nEncerrado.");
  process.exitCode = code;
  setTimeout(() => {
    process.reallyExit(code);
  }, 250).unref();


}

if (lobbySecret) {
  let startedLobbyVoice = false;
  timeouts.push(setInterval(() => {
    if (startedLobbyVoice) return;
    const status = discord.getClientStatus();
    if (status !== "Ready") return;
    startedLobbyVoice = true;
    console.log("startLobbyVoice:", discord.startLobbyVoice(lobbySecret));
  }, 1000));

  timeouts.push(setInterval(() => {
    try {
      console.log("voiceStats:", discord.getVoiceStats());
    } catch (error) {
      console.error(error);
    }
  }, 5000));
}

process.once("SIGINT", shutdown);
process.once("SIGTERM", shutdown);
process.once("uncaughtException", (error) => {
  console.error(error);
  shutdown(1);
});
process.once("unhandledRejection", (error) => {
  console.error(error);
  shutdown(1);
});
