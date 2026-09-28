"""Check native text entry names against an installed Houdini, without loading DLLs."""

import os
from pathlib import Path
import re
import struct
import unittest


ROOT = Path(__file__).resolve().parents[2]


def exported_names(path):
    data = path.read_bytes()
    u16 = lambda offset: struct.unpack_from('<H', data, offset)[0]
    u32 = lambda offset: struct.unpack_from('<I', data, offset)[0]
    pe = u32(0x3C)
    optional = pe + 24
    sections = []
    for index in range(u16(pe + 6)):
        section = optional + u16(pe + 20) + index * 40
        sections.append((u32(section + 12),
                         max(u32(section + 8), u32(section + 16)),
                         u32(section + 20)))

    def offset(rva):
        for address, size, raw in sections:
            if address <= rva < address + size:
                return raw + rva - address
        raise ValueError(f'Unmapped PE address: {rva:x}')

    directory = offset(u32(optional + (112 if u16(optional) == 0x20B else 96)))
    names = offset(u32(directory + 32))
    result = set()
    for index in range(u32(directory + 24)):
        start = offset(u32(names + index * 4))
        result.add(data[start:data.index(0, start)].decode('ascii'))
    return result


class NativeTextExportsTest(unittest.TestCase):
    def test_text_and_measurement_symbols_exist_in_target_houdini(self):
        selected = os.environ.get('HFS')
        if selected:
            installations = [Path(selected)]
        else:
            base = Path(os.environ.get('ProgramFiles', 'C:/Program Files'))
            installations = sorted((base / 'Side Effects Software').glob('Houdini 22.*'))
        if not installations:
            self.skipTest('No target Houdini installation; set HFS to validate another build')

        source = (ROOT / 'source/hook.cpp').read_text(encoding='utf-8')
        for installer, end, library, expected_count in (
            ('bool installUITextRangeHooks()', 'void hookedRERenderPutString', 'libUI.dll', 3),
            ('bool installRETextHooks()', 'void hookedUIHelpTipLookRender', 'libRE.dll', 5),
        ):
            start = source.index(installer)
            body = source[start:source.index(end, start)]
            # Join adjacent C++ string literals (some decorated names wrap lines).
            strings = re.findall(r'"[^"\n]*"(?:\s*"[^"\n]*")*', body)
            symbols = [''.join(re.findall(r'"([^"\n]*)"', value)) for value in strings]
            symbols = [value for value in symbols if value.startswith('?')]
            self.assertEqual(len(symbols), expected_count, installer)
            for installation in installations:
                exports = exported_names(installation / 'bin' / library)
                for symbol in symbols:
                    with self.subTest(build=installation.name, symbol=symbol):
                        self.assertIn(symbol, exports)


if __name__ == '__main__':
    unittest.main()
