const path = require("node:path");
const sdkBinPath = path.join(__dirname, "lib", "discord_social_sdk", "bin", "release");
process.env.PATH = `${sdkBinPath}${path.delimiter}${process.env.PATH || ""}`;
const discord = require("./build/Release/discord_social.node");
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

const interval = setInterval(() => {
  discord.runCallbacks();
}, 10);

process.on("SIGINT", () => {
  clearInterval(interval);
  discord.destroyClient();
  console.log("\nEncerrado.");
  process.exit(0);
});
