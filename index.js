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
  console.error("Uso: node index.js <DISCORD_APPLICATION_ID>");
  console.error("Ou defina DISCORD_APPLICATION_ID no ambiente.");
  process.exit(1);
}

console.log("Exports do binding:", Object.keys(discord));
console.log("initClient:", discord.initClient(String(applicationId)));
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

const messageTimeout = null;

function shutdown(code = 0) {
  if (shuttingDown) {
    process.reallyExit(code);
    return;
  }

  shuttingDown = true;
  clearInterval(interval);
  if (messageTimeout) {
    clearTimeout(messageTimeout);
  }
  try {
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

setTimeout(() => {
  discord.sendMessage("Lipinho", "Hello from Node.js!");
}, 1000*20);

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
