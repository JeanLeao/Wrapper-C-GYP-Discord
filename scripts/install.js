const https = require("https");
const fs = require("fs");
const path = require("path");
const { execSync } = require("child_process");

const LIB_DIR = path.join(__dirname, "..", "lib");
const ZIP_PATH = path.join(__dirname, "..", "lib.zip");
const REPO = "JeanLeao/Wrapper-C-GYP-Discord";
const ASSET_NAME = "lib.zip";

if (fs.existsSync(LIB_DIR)) {
  console.log("lib/ já existe, pulando download.");
  process.exit(0);
}

function get(url, redirects = 5) {
  return new Promise((resolve, reject) => {
    if (redirects === 0) return reject(new Error("Muitos redirecionamentos"));
    https.get(url, { headers: { "User-Agent": "node" } }, (res) => {
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
        return resolve(get(res.headers.location, redirects - 1));
      }
      if (res.statusCode !== 200) {
        return reject(new Error(`HTTP ${res.statusCode} em ${url}`));
      }
      resolve(res);
    }).on("error", reject);
  });
}

async function getLatestAssetUrl() {
  const res = await get(`https://api.github.com/repos/${REPO}/releases/latest`);
  const data = await new Promise((resolve, reject) => {
    let body = "";
    res.on("data", (c) => (body += c));
    res.on("end", () => resolve(JSON.parse(body)));
    res.on("error", reject);
  });

  const asset = data.assets && data.assets.find((a) => a.name === ASSET_NAME);
  if (!asset) throw new Error(`Asset "${ASSET_NAME}" não encontrado na última release.`);
  return asset.browser_download_url;
}

async function download(url, dest) {
  const res = await get(url);
  return new Promise((resolve, reject) => {
    const file = fs.createWriteStream(dest);
    res.pipe(file);
    file.on("finish", () => file.close(resolve));
    file.on("error", reject);
  });
}

function unzip(zipPath, destDir) {
  if (process.platform === "win32") {
    execSync(`powershell -Command "Expand-Archive -Path '${zipPath}' -DestinationPath '${destDir}' -Force"`);
  } else {
    execSync(`unzip -o "${zipPath}" -d "${destDir}"`);
  }
}

(async () => {
  try {
    console.log("Buscando URL do lib.zip na última release...");
    const url = await getLatestAssetUrl();
    console.log(`Baixando ${url}...`);
    await download(url, ZIP_PATH);
    console.log("Extraindo...");
    unzip(ZIP_PATH, path.join(__dirname, ".."));
    fs.unlinkSync(ZIP_PATH);
    console.log("lib/ instalada com sucesso.");
  } catch (err) {
    console.error("Erro ao baixar lib/:", err.message);
    console.error(`Baixe manualmente o ${ASSET_NAME} da release e extraia na raiz do projeto.`);
    process.exit(1);
  }
})();
