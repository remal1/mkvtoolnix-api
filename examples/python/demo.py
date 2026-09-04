"""
demo.py - Teljes értékű Python demonstráció és tesztprogram az mkvtoolnix.dll C ABI használatára.
"""

import sys
import os
import time
import json
import subprocess
from pathlib import Path

# Add local directory to path
sys.path.insert(0, str(Path(__file__).parent))
from mkvtoolnix import MkvLibrary, MkvContext, MkvError

if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

def main():
    print("================================================================================")
    print(" MKVToolNix C ABI Python FFI Integrációs és Paritásteszt")
    print("================================================================================\n")

    # 1. Könyvtár és Verzió-ellenőrzés
    lib = MkvLibrary()
    api_ver = lib.dll.mtx_api_version()
    print(f"[1] DLL sikeresen betöltve. C API Major Verzió: {api_ver}")

    # 2. Média-identifikáció és JSON lekérdezés
    sample_candidates = [
        Path(__file__).parent.parent.parent / "third_party" / "mkvtoolnix" / "share" / "sounds" / "finished-1.webm",
        Path(__file__).parent.parent.parent / "mkvtoolnix" / "share" / "sounds" / "finished-1.webm",
    ]
    sample_file = None
    for cand in sample_candidates:
        if cand.exists():
            sample_file = str(cand)
            break

    if not sample_file:
        print(f"Hiba: A tesztfájl nem található: {sample_candidates[0]}")
        return

    print(f"\n[2] Médiafájl azonosítása (Identify & JSON): {sample_file}")
    with MkvContext(lib) as ctx:
        with ctx.create_merge() as merge:
            input_media = merge.add_input(sample_file)
            
            # C Struct információk
            file_info = input_media.get_file_info()
            print(f"    Konténer formátum: {file_info['container_format']}")
            print(f"    Játékidő: {file_info['duration_ns'] / 1_000_000_000:.2f} másodperc")
            print(f"    Sávok száma: {file_info['track_count']}")

            # JSON információk
            id_data = input_media.identify_json()
            for t in id_data.get("tracks", []):
                print(f"    -> Sáv #{t['id']}: {t['type']} | Kodek: {t['codec']} | Nyelv: {t['properties'].get('language')}")

    # 3. Teljes Muxing Típusbiztos Sávbeállításokkal és Progress Callbackkel
    output_lib = "python_muxed.mkv"
    if os.path.exists(output_lib):
        os.remove(output_lib)

    print(f"\n[3] Muxing végrehajtása C API-val -> {output_lib}")
    progress_updates = []

    def on_progress(percentage, cur_sec, tot_sec, bytes_wr):
        progress_updates.append(percentage)
        sys.stdout.write(f"\r    [Progress] {percentage}% ({cur_sec:.2f}s / {tot_sec:.2f}s) | {bytes_wr // 1024} KB kiírva")
        sys.stdout.flush()

    with MkvContext(lib) as ctx:
        with ctx.create_merge() as merge:
            merge.set_output(output_lib)
            merge.set_title("Python FFI Remux Teszt")
            merge.set_default_language("hun")
            merge.set_deterministic(True)
            merge.on_progress(on_progress)

            inp = merge.add_input(sample_file)
            merge.prepare()

            tracks = inp.get_tracks()
            print(f"    Felderített sávok száma: {len(tracks)}")
            for trk in tracks:
                ttype = trk.get_type()
                tcodec = trk.get_codec()
                print(f"    Sáv konfigurálása: ID={trk.id}, Típus={ttype}, Kodek={tcodec}")
                trk.set_language("hun")
                trk.set_name("Magyar Opus Audio")
                trk.set_default(True)
                trk.set_delay(50)  # +50 ms késleltetés

            print("    Muxing indítása...")
            start_t = time.time()
            merge.execute()
            elapsed = time.time() - start_t
            print(f"\n    Muxing sikeresen befejeződött {elapsed:.3f} mp alatt!")

    assert os.path.exists(output_lib), "A kimeneti fájl nem jött létre!"
    assert 100 in progress_updates or len(progress_updates) > 0, "A progress callback nem futott le!"
    print(f"    Generált fájl mérete: {os.path.getsize(output_lib)} bájt.")

    # 4. Megszakítás (Cancellation) Tesztelése
    print("\n[4] Megszakítás (Cancellation) tesztelése...")
    cancel_output = "cancel_test.mkv"
    if os.path.exists(cancel_output):
        os.remove(cancel_output)

    with MkvContext(lib) as ctx:
        with ctx.create_merge() as merge:
            merge.set_output(cancel_output)
            merge.add_input(sample_file)
            merge.prepare()

            # Még a futás előtt kérünk egy azonnali cancel-t
            merge.cancel()

            cancelled = False
            try:
                merge.execute()
            except MkvError as ex:
                if ex.code == -6:
                    cancelled = True
                    print(f"    Sikeres megszakítás: {ex} (Kód: {ex.code})")

            assert cancelled, "A megszakításnak hibakódot kellett volna kiváltania!"
            assert not os.path.exists(cancel_output), "A félkész fájlnak törlődnie kellett volna!"
            print("    Megszakítás és automatikus takarítás sikeresen igazolva!")

    # 5. CLI vs. Library Paritás Összehasonlítás
    print("\n[5] CLI vs. Library Paritás Teszt...")
    output_cli = "cli_muxed.mkv"
    if os.path.exists(output_cli):
        os.remove(output_cli)

    # CLI parancs futtatása azonos beállításokkal
    cli_cmd = [
        "mkvmerge",
        "-o", output_cli,
        "--title", "Python FFI Remux Teszt",
        "--default-language", "hun",
        "--language", "0:hun",
        "--track-name", "0:Magyar Opus Audio",
        "--default-track", "0:1",
        "--sync", "0:50",
        sample_file
    ]

    try:
        subprocess.run(cli_cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        print("    mkvmerge CLI sikeresen lefutott azonos paraméterekkel.")

        # Mindkét kimenet azonosítása a JSON API segítségével
        with MkvContext(lib) as ctx:
            with ctx.create_merge() as merge:
                inp_lib = merge.add_input(output_lib)
                json_lib = inp_lib.identify_json()

            with ctx.create_merge() as merge:
                inp_cli = merge.add_input(output_cli)
                json_cli = inp_cli.identify_json()

        # Paritás ellenőrzése a két kimenet között
        track_lib = json_lib["tracks"][0]
        track_cli = json_cli["tracks"][0]

        print("\n    Összehasonlítási eredmények (CLI vs Library):")
        print(f"    - Konténer típus:        CLI: {json_cli['container']['type']} | LIB: {json_lib['container']['type']}")
        print(f"    - Sávok száma:           CLI: {len(json_cli['tracks'])} | LIB: {len(json_lib['tracks'])}")
        print(f"    - Sáv kodek:             CLI: {track_cli['codec']} | LIB: {track_lib['codec']}")
        print(f"    - Sáv nyelv:             CLI: {track_cli['properties']['language']} | LIB: {track_lib['properties']['language']}")
        print(f"    - Sáv név:               CLI: {track_cli['properties']['track_name']} | LIB: {track_lib['properties']['track_name']}")
        print(f"    - Default flag:          CLI: {track_cli['properties']['default_track']} | LIB: {track_lib['properties']['default_track']}")

        assert json_cli['container']['type'] == json_lib['container']['type'], "Konténer típus eltér!"
        assert len(json_cli['tracks']) == len(json_lib['tracks']), "Sávok száma eltér!"
        assert track_cli['codec'] == track_lib['codec'], "Kodek eltér!"
        assert track_cli['properties']['language'] == track_lib['properties']['language'], "Nyelv eltér!"
        assert track_cli['properties']['track_name'] == track_lib['properties']['track_name'], "Sávnév eltér!"
        assert track_cli['properties']['default_track'] == track_lib['properties']['default_track'], "Default flag eltér!"

        print("\n *** MINDEN PARITÁSI TESZT SIKERESEN ÁTMENT (100% EGYEZÉS)! ***")

    except FileNotFoundError:
        print("    Megjegyzés: mkvmerge CLI nem érhető el közvetlenül, a CLI összehasonlítás kihagyva.")

    print("\n================================================================================")
    print(" Teszt sikeresen befejeződött!")
    print("================================================================================")

if __name__ == "__main__":
    main()
