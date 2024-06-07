const tar = require("tar");
const fs = require("fs");
const fetch = require("node-fetch");

const NPM_URL = "https://registry.npmjs.org";
const ROOT = "public/assets";

const FFMPEG_VERSION = "0.12.7";
const UTIL_VERSION = "0.12.0";
const CORE_VERSION = "0.12.5";
const CORE_MT_VERSION = "0.12.5";

const FFMPEG_TGZ = `ffmpeg-${FFMPEG_VERSION}.tgz`;
const UTIL_TGZ = `util-${UTIL_VERSION}.tgz`;
const CORE_TGZ = `core-${CORE_VERSION}.tgz`;
const CORE_MT_TGZ = `core-mt-${CORE_MT_VERSION}.tgz`;

const FFMPEG_TGZ_URL = `${NPM_URL}/@ffmpeg/ffmpeg/-/${FFMPEG_TGZ}`;
const UTIL_TGZ_URL = `${NPM_URL}/@ffmpeg/util/-/${UTIL_TGZ}`;
const CORE_TGZ_URL = `${NPM_URL}/@ffmpeg/core/-/${CORE_TGZ}`;
const CORE_MT_TGZ_URL = `${NPM_URL}/@ffmpeg/core-mt/-/${CORE_MT_TGZ}`;

const mkdir = (dir) => {
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
    console.log(`Created directory: ${dir}`);
  }
};

const downloadAndUntar = async (url, tgzName, dst) => {
  const dir = `${ROOT}/${dst}`;
  if (fs.existsSync(dir)) {
    console.log(`Found @ffmpeg/${dst} assets.`);
    return;
  }

  console.log(`Downloading and extracting ${url}...`);
  mkdir(dir);

  try {
    const response = await fetch(url);
    if (!response.ok) {
      throw new Error(`Failed to download ${url}: ${response.statusText}`);
    }

    const data = Buffer.from(await response.arrayBuffer());
    fs.writeFileSync(tgzName, data);
    console.log(`Downloaded ${tgzName}`);

    await tar.x({ file: tgzName, C: dir });
    console.log(`Extracted ${tgzName} to ${dir}`);

    fs.unlinkSync(tgzName);
    console.log(`Deleted temporary file ${tgzName}`);
  } catch (error) {
    console.error(`Error downloading or extracting ${url}: ${error.message}`);
  }
};

const init = async () => {
  mkdir(ROOT);

  await downloadAndUntar(FFMPEG_TGZ_URL, FFMPEG_TGZ, "ffmpeg");
  await downloadAndUntar(UTIL_TGZ_URL, UTIL_TGZ, "util");
  await downloadAndUntar(CORE_TGZ_URL, CORE_TGZ, "core");
  await downloadAndUntar(CORE_MT_TGZ_URL, CORE_MT_TGZ, "core-mt");
};

init().then(() => {
  console.log('Download and extraction complete.');
}).catch(error => {
  console.error(`Initialization failed: ${error.message}`);
});
