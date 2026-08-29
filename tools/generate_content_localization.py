#!/usr/bin/env python3
"""Generate the embedded TH07 dialogue and Music Room translation tables."""

from __future__ import annotations

import json
import re
import struct
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERVER = "https://srv.thpatch.net"
LANGUAGES = ("lang_zh-hans", "lang_en")
ASSET_ARCHIVE = ROOT / "assets" / "th07.dat"

MUSIC_TITLES_ZH = (
    "妖妖梦 ～ Snow or Cherry Petal",
    "无何有之乡 ～ Deep Mountain",
    "水晶银",
    "远野幻想物语",
    "凋叶棕（withered leaf）",
    "布加勒斯特的人偶师",
    "人偶裁判 ～ 玩弄人形的少女",
    "天空的花都",
    "幽灵乐团 ～ Phantom Ensemble",
    "东方妖妖梦 ～ Ancient Temple",
    "广有射怪鸟事 ～ Till When?",
    "究极真实",
    "幽雅地绽放吧，墨染的樱花 ～ Border of Life",
    "生死之境",
    "妖妖跋扈",
    "少女幻葬 ～ Necro-Fantasy",
    "妖妖跋扈 ～ Who done it!",
    "Necrofantasia",
    "春风之梦",
    "樱花樱花 ～ Japanize Dream...",
)

MUSIC_TITLES_EN = (
    "Mystical Dream ~ Snow or Cherry Petal",
    "Paradise ~ Deep Mountain",
    "Crystallized Silver",
    "The Fantastic Tales from Tono",
    "Diao ye zong (Withered Leaf)",
    "The Doll Maker of Bucuresti",
    "Doll Judgment ~ The Girl Who Played with People's Shapes",
    "The Capital City of Flowers in the Sky",
    "Ghostly Band ~ Phantom Ensemble",
    "Eastern Ghostly Dream ~ Ancient Temple",
    "Hiroari Shoots a Strange Bird ~ Till When?",
    "Ultimate Truth",
    "Bloom Nobly, Ink-Black Cherry Blossom ~ Border of Life",
    "Border of Life",
    "Spiritual Domination",
    "A Maiden's Illusionary Funeral ~ Necro-Fantasy",
    "Spiritual Domination ~ Who done it!",
    "Necrofantasia",
    "Dream of a Spring Breeze",
    "Sakura, Sakura ~ Japanize Dream...",
)


def load_json(url: str):
    request = urllib.request.Request(url, headers={"User-Agent": "th07-ios-port"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def lzss_decompress(source: bytes, output_size: int) -> bytes:
    dictionary = bytearray(8192)
    dictionary_head = 1
    output = bytearray()
    source_pos = 0
    bit_mask = 0x80
    current_byte = 0

    def read_bit() -> int:
        nonlocal source_pos, bit_mask, current_byte
        if bit_mask == 0x80:
            current_byte = source[source_pos] if source_pos < len(source) else 0
            source_pos += 1
        value = 1 if current_byte & bit_mask else 0
        bit_mask >>= 1
        if bit_mask == 0:
            bit_mask = 0x80
        return value

    def read_bits(count: int) -> int:
        value = 0
        for _ in range(count):
            value = (value << 1) | read_bit()
        return value

    while len(output) < output_size:
        if read_bit():
            value = read_bits(8)
            output.append(value)
            dictionary[dictionary_head] = value
            dictionary_head = (dictionary_head + 1) & 8191
        else:
            offset = read_bits(13)
            if offset == 0:
                break
            for index in range(read_bits(4) + 3):
                value = dictionary[(offset + index) & 8191]
                output.append(value)
                dictionary[dictionary_head] = value
                dictionary_head = (dictionary_head + 1) & 8191
                if len(output) >= output_size:
                    break
    if len(output) != output_size:
        raise RuntimeError(f"PBG4 entry decompressed to {len(output)}, expected {output_size}")
    return bytes(output)


def load_pbg4_entries() -> dict[str, bytes]:
    data = ASSET_ARCHIVE.read_bytes()
    magic, count, header_offset, header_size = struct.unpack_from("<4sIII", data)
    if magic != b"PBG4":
        raise RuntimeError("assets/th07.dat is not a PBG4 archive")
    header = lzss_decompress(data[header_offset:], header_size)
    entries = []
    position = 0
    for _ in range(count):
        end = header.index(0, position)
        name = header[position:end].decode("ascii")
        position = end + 1
        offset, size, _ = struct.unpack_from("<III", header, position)
        position += 12
        entries.append((name, offset, size))
    result = {}
    for index, (name, offset, size) in enumerate(entries):
        compressed_end = entries[index + 1][1] if index + 1 < len(entries) else header_offset
        result[name] = lzss_decompress(data[offset:compressed_end], size)
    return result


def load_message_instructions(entries: dict[str, bytes]):
    scripts = {}
    for stage in range(1, 9):
        data = entries[f"msg{stage}.dat"]
        count = struct.unpack_from("<i", data)[0]
        offsets = struct.unpack_from(f"<{count}I", data, 4)
        for script_id, offset in enumerate(offsets):
            instructions = []
            position = offset
            same_time_counts = {}
            while position + 4 <= len(data):
                event_time, opcode, argument_size = struct.unpack_from("<HBB", data, position)
                arguments = data[position + 4:position + 4 + argument_size]
                line = struct.unpack_from("<h", arguments, 2)[0] if opcode in (3, 8) else 0
                count_key = (event_time, opcode)
                occurrence = same_time_counts.get(count_key, 0)
                same_time_counts[count_key] = occurrence + 1
                instructions.append({
                    "time": event_time,
                    "opcode": opcode,
                    "line": line,
                    "occurrence": occurrence,
                })
                position += 4 + argument_size
                if opcode == 0:
                    break
            scripts[(stage, script_id)] = instructions
    return scripts


def clean_line(line: str) -> str:
    # thcrap's \x14 suffix contains translator notes which are not in-game text.
    line = line.split("\x14", 1)[0]
    line = re.sub(r"<[^>]*>", "", line)
    return line.rstrip()


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def load_dialogue(language: str, scripts):
    result = {}
    for stage in range(1, 9):
        document = load_json(f"{SERVER}/{language}/th07/msg{stage}.dat.jdiff")
        for script_text, script in document.items():
            script_id = int(script_text)
            parsed_entries = []
            for key, value in script.items():
                regular = re.fullmatch(r"(\d+)_(\d+)", key)
                header = re.fullmatch(r"(\d+)_h1_(\d+)", key)
                if regular:
                    event_time, occurrence = map(int, regular.groups())
                    opcode = 3
                elif header:
                    event_time, occurrence = map(int, header.groups())
                    opcode = 8
                else:
                    raise RuntimeError(f"Unsupported TH07 message key: {key}")
                candidates = [
                    index for index, instruction in enumerate(scripts[(stage, script_id)])
                    if instruction["time"] == event_time and instruction["opcode"] == opcode
                ]
                # Header entries identify the group, so their suffix counts groups rather
                # than each of the two physical intro-line instructions.
                if opcode == 8:
                    candidates = [index for index in candidates
                                  if scripts[(stage, script_id)][index]["line"] == 0]
                if occurrence >= len(candidates):
                    raise RuntimeError(
                        f"No original instruction for stage {stage}, script {script_id}, {key}")
                parsed_entries.append((candidates[occurrence], opcode, value.get("lines", ())))

            for entry_index, (start, opcode, translated_lines) in enumerate(
                    sorted(parsed_entries, key=lambda item: item[0])):
                following = [item[0] for item in parsed_entries
                             if item[1] == opcode and item[0] > start]
                end = min(following) if following else len(scripts[(stage, script_id)])
                physical = [instruction for instruction in scripts[(stage, script_id)][start:end]
                            if instruction["opcode"] == opcode]
                cleaned = [clean_line(line) for line in translated_lines]
                if len(cleaned) > len(physical):
                    # The engine has no extra physical VM line. Preserve all text in the
                    # final available line rather than silently discarding translation.
                    cleaned = cleaned[:len(physical) - 1] + [
                        " ".join(cleaned[len(physical) - 1:])
                    ]
                for line_index, instruction in enumerate(physical):
                    text = cleaned[line_index] if line_index < len(cleaned) else ""
                    result[(stage, script_id, instruction["time"], opcode,
                            instruction["occurrence"], instruction["line"])] = text
    return result


def load_music(language: str):
    document = load_json(f"{SERVER}/{language}/th07/musiccmt.js")
    tracks = []
    for track in range(1, 21):
        lines = [clean_line(line) for line in document[str(track)] if line != "@"]
        if len(lines) > 7:
            raise RuntimeError(f"Music Room track {track} has too many comment lines")
        tracks.append(lines)
    return tracks


def generate() -> str:
    scripts = load_message_instructions(load_pbg4_entries())
    zh = load_dialogue(LANGUAGES[0], scripts)
    en = load_dialogue(LANGUAGES[1], scripts)
    if zh.keys() != en.keys():
        raise RuntimeError("Chinese and English dialogue keys differ")
    music_zh = load_music(LANGUAGES[0])
    music_en = load_music(LANGUAGES[1])

    lines = [
        '// Generated by tools/generate_content_localization.py.',
        '// Dialogue and Music Room comments originate from the Touhou Patch Center',
        '// lang_zh-hans and lang_en TH07 translation resources.',
        '#include "Localization.hpp"',
        '',
        '#include <cstddef>',
        '',
        'namespace',
        '{',
        'struct DialogueEntry',
        '{',
        '    unsigned char stage;',
        '    unsigned char script;',
        '    unsigned short time;',
        '    unsigned char opcode;',
        '    unsigned char occurrence;',
        '    unsigned char line;',
        '    const char *chinese;',
        '    const char *english;',
        '};',
        '',
        'constexpr DialogueEntry kDialogueEntries[] = {',
    ]
    for key in sorted(zh):
        stage, script, event_time, opcode, occurrence, line = key
        lines.append(
            f'    {{{stage}, {script}, {event_time}, {opcode}, {occurrence}, {line}, '
            f'{cpp_string(zh[key])}, {cpp_string(en[key])}}},'
        )
    lines.extend([
        '};',
        '',
        'struct MusicEntry',
        '{',
        '    const char *chineseTitle;',
        '    const char *englishTitle;',
        '    const char *chineseComments[7];',
        '    const char *englishComments[7];',
        '};',
        '',
        'constexpr MusicEntry kMusicEntries[] = {',
    ])
    for track in range(20):
        zh_comments = music_zh[track] + [""] * (7 - len(music_zh[track]))
        en_comments = music_en[track] + [""] * (7 - len(music_en[track]))
        lines.append('    {')
        lines.append(f'        {cpp_string(MUSIC_TITLES_ZH[track])},')
        lines.append(f'        {cpp_string(MUSIC_TITLES_EN[track])},')
        lines.append('        {' + ', '.join(cpp_string(x) for x in zh_comments) + '},')
        lines.append('        {' + ', '.join(cpp_string(x) for x in en_comments) + '},')
        lines.append('    },')
    lines.extend([
        '};',
        '} // namespace',
        '',
        'namespace Localization',
        '{',
        'std::string TranslateDialogue(int stage, int script, int time, int opcode,',
        '                              int occurrence, int line, const char *fallback,',
        '                              bool *translated)',
        '{',
        '    if (translated) *translated = false;',
        '    if (GetLanguage() == Language::Japanese)',
        '        return fallback ? fallback : "";',
        '    for (const DialogueEntry &entry : kDialogueEntries)',
        '    {',
        '        if (entry.stage != stage || entry.script != script || entry.time != time ||',
        '            entry.opcode != opcode || entry.occurrence != occurrence ||',
        '            entry.line != line)',
        '            continue;',
        '        const char *value = GetLanguage() == Language::Chinese',
        '                                ? entry.chinese : entry.english;',
        '        if (translated) *translated = true;',
        '        return value ? value : "";',
        '    }',
        '    return fallback ? fallback : "";',
        '}',
        '',
        'std::string TranslateMusicTitle(int track, const char *fallback, bool *translated)',
        '{',
        '    if (translated) *translated = false;',
        '    if (GetLanguage() == Language::Japanese || track < 0 ||',
        '        track >= static_cast<int>(sizeof(kMusicEntries) / sizeof(kMusicEntries[0])))',
        '        return fallback ? fallback : "";',
        '    const MusicEntry &entry = kMusicEntries[track];',
        '    const char *value = GetLanguage() == Language::Chinese',
        '                            ? entry.chineseTitle : entry.englishTitle;',
        '    if (translated) *translated = true;',
        '    return value ? value : "";',
        '}',
        '',
        'std::string TranslateMusicComment(int track, int line, const char *fallback,',
        '                                  bool *translated)',
        '{',
        '    if (translated) *translated = false;',
        '    if (GetLanguage() == Language::Japanese || track < 0 || line < 0 || line >= 7 ||',
        '        track >= static_cast<int>(sizeof(kMusicEntries) / sizeof(kMusicEntries[0])))',
        '        return fallback ? fallback : "";',
        '    const MusicEntry &entry = kMusicEntries[track];',
        '    const char *value = GetLanguage() == Language::Chinese',
        '                            ? entry.chineseComments[line] : entry.englishComments[line];',
        '    if (translated) *translated = true;',
        '    return value ? value : "";',
        '}',
        '} // namespace Localization',
        '',
    ])
    return "\n".join(lines)


if __name__ == "__main__":
    output = ROOT / "src" / "ContentLocalization.cpp"
    output.write_text(generate(), encoding="utf-8", newline="\n")
    print(f"generated {output}")
