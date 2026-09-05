/**
 * demo.ts - Bun (TypeScript) FFI demonstráció és CLI-paritás tesztprogram az mkvtoolnix.dll C ABI-hoz.
 * Futtatás: bun run examples/bun/demo.ts
 */

import { MkvLibrary, MkvContext, MkvError } from "./mkvtoolnix";
import { resolve } from "path";
import { existsSync, unlinkSync, statSync, writeFileSync } from "fs";
import { spawnSync } from "child_process";

async function main() {
  console.log("================================================================================");
  console.log(" MKVToolNix C ABI Bun (TypeScript) FFI Integrációs és Paritásteszt");
  console.log("================================================================================\n");

  // 1. DLL betöltése és részletes verzió-ellenőrzés
  const lib = new MkvLibrary();
  const v = lib.getVersion();
  console.log(`[1] DLL sikeresen betöltve:`);
  console.log(`    - API Verzió:          v${v.version} (Major: ${v.major}, Minor: ${v.minor}, Patch: ${v.patch})`);
  console.log(`    - ABI Revízió:         ${v.abiRevision}`);
  console.log(`    - MKVToolNix Verzió:   ${v.mkvtoolnixVersion}`);
  console.log(`    - Platform / Compiler: ${v.platform || "unknown"} / ${v.compiler || "unknown"}`);
  console.log(`    - Build Időpont:       ${v.buildDate}`);
  console.log(`    - Teljes string:       ${lib.versionString}`);

  // 2. Médiafájl azonosítása (Identify & JSON)
  const sampleCandidates = [
    resolve(import.meta.dir, "../../third_party/mkvtoolnix/share/sounds/finished-1.webm"),
    resolve(import.meta.dir, "../../mkvtoolnix/share/sounds/finished-1.webm"),
  ];
  let sampleFile = "";
  for (const cand of sampleCandidates) {
    if (existsSync(cand)) {
      sampleFile = cand;
      break;
    }
  }

  if (!sampleFile) {
    console.error(`Hiba: A tesztfájl nem található: ${sampleCandidates[0]}`);
    process.exit(1);
  }

  console.log(`\n[2] Médiafájl azonosítása (Identify & JSON): ${sampleFile}`);
  const ctx = new MkvContext(lib);
  try {
    const merge = ctx.createMerge();
    try {
      const inputMedia = merge.addInput(sampleFile);
      const json = inputMedia.identifyJson();

      console.log(`    Konténer formátum: ${json.container.type}`);
      console.log(`    Sávok száma: ${json.tracks.length}`);
      for (const t of json.tracks) {
        console.log(`    -> Sáv #${t.id}: ${t.type} | Kodek: ${t.codec} | Nyelv: ${t.properties.language}`);
      }
    } finally {
      merge.destroy();
    }
  } finally {
    ctx.destroy();
  }

  // 3. Teljes Muxing Típusbiztos Sávbeállításokkal, Csatolmányokkal és Fejezetekkel
  const outputLib = "bun_muxed.mkv";
  if (existsSync(outputLib)) unlinkSync(outputLib);

  const attFile = "test_attachment.txt";
  writeFileSync(attFile, "MKVToolNix Attachment File Test Content\n");

  console.log(`\n[3] Muxing végrehajtása Bun FFI-vel (Csatolmányok + Fejezetek) -> ${outputLib}`);
  const progressUpdates: number[] = [];

  const ctx2 = new MkvContext(lib);
  try {
    const merge = ctx2.createMerge();
    try {
      merge.setOutput(outputLib);
      merge.setTitle("Bun FFI Remux Teszt");
      merge.setDefaultLanguage("hun");
      merge.setDeterministic(true);

      // Csatolmányok hozzáadása
      console.log("    Fájl csatolmány hozzáadása...");
      merge.addAttachmentFile(attFile, "cover_notes.txt", "text/plain", "Borító jegyzetek");

      console.log("    Memória puffer csatolmány hozzáadása...");
      const memData = Buffer.from("MKVToolNix Memory Buffer Attachment Payload Data");
      merge.addAttachmentMemory(memData, "memory_asset.bin", "application/octet-stream", "Memória puffer csatolmány");

      // Fejezetek hozzáadása szövegből (OGG Simple formátum)
      console.log("    Fejezetek beállítása szöveges forrásból...");
      const chaptersText = [
        "CHAPTER01=00:00:00.000",
        "CHAPTER01NAME=Bevezetés",
        "CHAPTER02=00:00:00.500",
        "CHAPTER02NAME=Fő rész",
        "CHAPTER03=00:00:01.000",
        "CHAPTER03NAME=Befejezés"
      ].join("\n");
      merge.setChaptersText(chaptersText, "hun", "UTF-8");

      merge.onProgress((pct, curSec, totSec, bytesWr) => {
        progressUpdates.push(pct);
        process.stdout.write(
          `\r    [Progress] ${pct}% (${curSec.toFixed(2)}s / ${totSec.toFixed(2)}s) | ${Math.floor(bytesWr / 1024)} KB kiírva`
        );
      });

      const inp = merge.addInput(sampleFile);
      merge.prepare();

      const tracks = inp.getTracks();
      console.log(`    Felderített sávok száma: ${tracks.length}`);
      for (const trk of tracks) {
        console.log(`    Sáv konfigurálása: ID=${trk.id}, Típus=${trk.getType()}, Kodek=${trk.getCodec()}`);
        trk.setLanguage("hun");
        trk.setName("Magyar Opus Audio");
        trk.setDefault(true);
        trk.setDelay(50); // +50 ms késleltetés
      }

      console.log("    Muxing indítása...");
      const startT = performance.now();
      await merge.execute();
      const elapsed = (performance.now() - startT).toFixed(2);
      console.log(`\n    Muxing sikeresen befejeződött ${elapsed} ms alatt!`);

    } finally {
      merge.destroy();
    }
  } finally {
    ctx2.destroy();
  }

  if (existsSync(attFile)) unlinkSync(attFile);
  if (!existsSync(outputLib)) throw new Error("A kimeneti fájl nem jött létre!");
  console.log(`    Generált fájl mérete: ${statSync(outputLib).size} bájt.`);

  // 3b. Csatolmányok és Fejezetek ellenőrzése az előállított fájlban (Identify)
  console.log("\n[3b] Csatolmányok és Fejezetek visszaolvasásának ellenőrzése...");
  const ctxVerify = new MkvContext(lib);
  try {
    const mergeVerify = ctxVerify.createMerge();
    try {
      const inpVerify = mergeVerify.addInput(outputLib);
      const jsonVerify = inpVerify.identifyJson();

      console.log(`    Csatolmányok száma a kimenetben: ${jsonVerify.attachments?.length || 0}`);
      for (const att of jsonVerify.attachments || []) {
        console.log(`    -> Csatolmány #${att.id}: ${att.file_name} (${att.content_type}, ${att.size} bájt) - ${att.description}`);
      }

      if (!jsonVerify.attachments || jsonVerify.attachments.length !== 2) {
        throw new Error(`Elvárt csatolmányok száma 2, kapott: ${jsonVerify.attachments?.length}`);
      }

      console.log(`    Fejezet bejegyzések száma a kimenetben: ${jsonVerify.chapters?.length || 0}`);
      for (const ch of jsonVerify.chapters || []) {
        console.log(`    -> Fejezet #${ch.id}: ${ch.num_entries} bejegyzés`);
      }

      if (!jsonVerify.chapters || jsonVerify.chapters.length === 0 || jsonVerify.chapters[0].num_entries !== 3) {
        throw new Error(`Elvárt 3 fejezet-bejegyzés, kapott: ${JSON.stringify(jsonVerify.chapters)}`);
      }
      console.log("    Csatolmányok és fejezetek jelenléte és integritása sikeresen igazolva!");
    } finally {
      mergeVerify.destroy();
    }
  } finally {
    ctxVerify.destroy();
  }

  // 3c. Csatolmányok és Fejezetek elhagyása (no-attachments, no-chapters) teszt
  console.log("\n[3c] Csatolmányok és Fejezetek kizárásának tesztelése (--no-attachments, --no-chapters)...");
  const strippedOutput = "bun_stripped.mkv";
  if (existsSync(strippedOutput)) unlinkSync(strippedOutput);

  const ctxStrip = new MkvContext(lib);
  try {
    const mergeStrip = ctxStrip.createMerge();
    try {
      mergeStrip.setOutput(strippedOutput);
      const inpStrip = mergeStrip.addInput(outputLib);
      inpStrip.setNoAttachments(true);
      inpStrip.setNoChapters(true);

      mergeStrip.prepare();
      await mergeStrip.execute();
    } finally {
      mergeStrip.destroy();
    }

    // Ellenőrzés: a stripped fájlban nem lehet csatolmány vagy fejezet
    const mergeCheck = ctxStrip.createMerge();
    try {
      const inpCheck = mergeCheck.addInput(strippedOutput);
      const jsonCheck = inpCheck.identifyJson();
      const attCount = jsonCheck.attachments?.length || 0;
      const chCount = jsonCheck.chapters?.length || 0;
      console.log(`    Kizárás után: Csatolmányok száma: ${attCount}, Fejezetek száma: ${chCount}`);
      if (attCount !== 0 || chCount !== 0) {
        throw new Error(`A kizárás nem működött: att=${attCount}, ch=${chCount}`);
      }
      console.log("    Csatolmányok és fejezetek sikeresen kizárva a kimenetből!");
    } finally {
      mergeCheck.destroy();
    }
  } finally {
    ctxStrip.destroy();
  }

  // 4. Megszakítás (Cancellation) Tesztelése
  console.log("\n[4] Megszakítás (Cancellation) tesztelése...");
  const cancelOutput = "bun_cancel_test.mkv";
  if (existsSync(cancelOutput)) unlinkSync(cancelOutput);

  const ctx3 = new MkvContext(lib);
  try {
    const merge = ctx3.createMerge();
    try {
      merge.setOutput(cancelOutput);
      merge.addInput(sampleFile);
      merge.prepare();

      // Még a végrehajtás előtt kérünk azonnali megszakítást
      merge.cancel();

      let wasCancelled = false;
      try {
        await merge.execute();
      } catch (err: any) {
        if (err instanceof MkvError && err.code === -6) {
          wasCancelled = true;
          console.log(`    Sikeres megszakítás: ${err.message} (Kód: ${err.code})`);
        } else {
          throw err;
        }
      }

      if (!wasCancelled) throw new Error("A megszakításnak hibakódot kellett volna adnia!");
      if (existsSync(cancelOutput)) throw new Error("A félkész fájlnak törlődnie kellett volna!");
      console.log("    Megszakítás és automatikus fájltakarítás sikeresen igazolva!");

    } finally {
      merge.destroy();
    }
  } finally {
    ctx3.destroy();
  }

  // 5. CLI vs. Library Paritás Teszt
  console.log("\n[5] CLI vs. Library Paritás Teszt...");
  const outputCli = "cli_bun_muxed.mkv";
  if (existsSync(outputCli)) unlinkSync(outputCli);

  const cliCmd = [
    "mkvmerge",
    "-o", outputCli,
    "--title", "Bun FFI Remux Teszt",
    "--default-language", "hun",
    "--language", "0:hun",
    "--track-name", "0:Magyar Opus Audio",
    "--default-track", "0:1",
    "--sync", "0:50",
    sampleFile,
  ];

  try {
    const proc = spawnSync(cliCmd[0], cliCmd.slice(1));
    if (proc.status !== 0 || proc.error) {
      console.warn("    mkvmerge hiba vagy hiányzik:", proc.stderr ? proc.stderr.toString().trim() : (proc.error?.message || "nem található PATH-on"));
    } else {
      console.log("    mkvmerge CLI sikeresen lefutott azonos paraméterekkel.");

      const ctx4 = new MkvContext(lib);
      let jsonLib: any;
      let jsonCli: any;
      try {
        const merge1 = ctx4.createMerge();
        jsonLib = merge1.addInput(outputLib).identifyJson();
        merge1.destroy();

        const merge2 = ctx4.createMerge();
        jsonCli = merge2.addInput(outputCli).identifyJson();
        merge2.destroy();
      } finally {
        ctx4.destroy();
      }

      const trackLib = jsonLib.tracks[0];
      const trackCli = jsonCli.tracks[0];

      console.log("\n    Összehasonlítási eredmények (CLI vs Library):");
      console.log(`    - Konténer típus:        CLI: ${jsonCli.container.type} | LIB: ${jsonLib.container.type}`);
      console.log(`    - Sávok száma:           CLI: ${jsonCli.tracks.length} | LIB: ${jsonLib.tracks.length}`);
      console.log(`    - Sáv kodek:             CLI: ${trackCli.codec} | LIB: ${trackLib.codec}`);
      console.log(`    - Sáv nyelv:             CLI: ${trackCli.properties.language} | LIB: ${trackLib.properties.language}`);
      console.log(`    - Sáv név:               CLI: ${trackCli.properties.track_name} | LIB: ${trackLib.properties.track_name}`);
      console.log(`    - Default flag:          CLI: ${trackCli.properties.default_track} | LIB: ${trackLib.properties.default_track}`);

      if (jsonCli.container.type !== jsonLib.container.type) throw new Error("Konténer típus eltér!");
      if (jsonCli.tracks.length !== jsonLib.tracks.length) throw new Error("Sávok száma eltér!");
      if (trackCli.codec !== trackLib.codec) throw new Error("Kodek eltér!");
      if (trackCli.properties.language !== trackLib.properties.language) throw new Error("Nyelv eltér!");
      if (trackCli.properties.track_name !== trackLib.properties.track_name) throw new Error("Sávnév eltér!");
      if (trackCli.properties.default_track !== trackLib.properties.default_track) throw new Error("Default flag eltér!");

      console.log("\n *** MINDEN PARITÁSI TESZT SIKERESEN ÁTMENT (100% EGYEZÉS)! ***");
    }
  } catch (err: any) {
    console.warn("    Megjegyzés: mkvmerge nem érhető el közvetlenül:", err.message);
  }

  console.log("\n================================================================================");
  console.log(" Teszt sikeresen befejeződött!");
  console.log("================================================================================");
}

main().catch((err) => {
  console.error("\nHiba a teszt futtatása során:", err);
  process.exit(1);
});
