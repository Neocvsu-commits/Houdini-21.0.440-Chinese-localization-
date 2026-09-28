import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'python3.11libs'))
from houdini_chinese_tooltips21 import normalize_help_text, translate_html


class HelpHTMLTests(unittest.TestCase):
    def translate(self, text):
        return {'File': '文件', "It's a guide.": '这是一根引导线。',
                'Compare': 'A < B & C'}.get(text, '')

    def test_display_only(self):
        source = '<p>File</p><a href="/File?q=File" title="File">File</a>'
        self.assertEqual(translate_html(source, self.translate),
                         '<p>文件</p><a href="/File?q=File" title="File">文件</a>')

    def test_code_and_styles_keep_meaning(self):
        source = '<code>File</code><pre>File &lt; x</pre><script>x < y</script><style>.File{}</style>'
        self.assertEqual(translate_html(source, self.translate), source)

    def test_entities_and_escaping(self):
        self.assertEqual(translate_html('<p>It&#39;s a guide.</p>', self.translate),
                         '<p>这是一根引导线。</p>')
        self.assertEqual(translate_html('<b>Compare</b>', self.translate),
                         '<b>A &lt; B &amp; C</b>')

    def test_disabled_and_unknown(self):
        source = '<p>File &amp; unknown</p>'
        self.assertEqual(translate_html(source, lambda _: ''), source)

    def test_data_driven_recipe_help(self):
        self.assertEqual(
            normalize_help_text('shelftool::sidefx::recipe::mpm_configure_sand_instances'),
            'Puts down a simple network of nodes to simulate Crag walking through a sandbox.')
        self.assertEqual(
            normalize_help_text('shelftool::sidefx::recipe::pyro::aerialgroundbarrage'),
            'Creates a large scale aerial explosion that moves towards the ground.')
        self.assertEqual(normalize_help_text('ordinary help'), 'ordinary help')


if __name__ == '__main__':
    unittest.main()
