"""中文节点搜索的纯逻辑回归测试（不依赖 hou，可在普通 pytest 下跑）。"""
import os
import sys

import pytest

_MODULE_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "python3.11libs")
sys.path.insert(0, os.path.abspath(_MODULE_DIR))

import houdini_chinese_nodesearch21 as search_module  # noqa: E402


def _entry(name, label_en, label_zh, category="Sop"):
    entry = search_module.Entry(name, category, label_en, label_zh)
    search_module._prepare(entry)
    return entry


@pytest.fixture(scope="module")
def entries():
    return [
        _entry("box", "Box", "长方体"),
        _entry("transform", "Transform", "变换"),
        _entry("attribwrangle", "Attribute Wrangle", "属性处理"),
        _entry("v_plastic", "Plastic", "塑性"),
        _entry("noise", "Noise", "噪波"),
        _entry("uvmap", "UV Map", "UV 贴图"),
    ]


def _names(result):
    return [entry.name for entry in result]


def test_full_pinyin(entries):
    assert _names(search_module.search(entries, "changfangti"))[0] == "box"


def test_initials(entries):
    assert _names(search_module.search(entries, "cft"))[0] == "box"


def test_chinese(entries):
    assert _names(search_module.search(entries, "长方体"))[0] == "box"


def test_fuzzy_sh_to_s(entries):
    hits = _names(search_module.search(entries, "suxin"))
    assert "v_plastic" in hits and "attribwrangle" in hits


def test_missing_letter(entries):
    assert _names(search_module.search(entries, "bianhuan"))[0] == "transform"


def test_multiword_english(entries):
    assert _names(search_module.search(entries, "attr wrangle"))[0] == "attribwrangle"


def test_mixed_label_pinyin(entries):
    assert _names(search_module.search(entries, "uvtietu"))[0] == "uvmap"


def test_mixed_chinese_english(entries):
    # "uv贴图" 这类中英混输要能命中
    assert _names(search_module.search(entries, "uv贴图"))[0] == "uvmap"


def test_mixed_reverse_order(entries):
    assert _names(search_module.search(entries, "贴图uv"))[0] == "uvmap"


def test_mixed_two_words_across_languages(entries):
    assert _names(search_module.search(entries, "box长方体"))[0] == "box"
    assert _names(search_module.search(entries, "长方体box"))[0] == "box"


def test_nonsense_query_returns_nothing(entries):
    assert search_module.search(entries, "qwertyui") == []


def test_no_cross_word_noise(entries):
    # 跨半个词的子序列命中必须被拒绝
    assert search_module.search(entries, "fatie") == []


def test_soft_form_folds_nasals():
    assert search_module.soft_form("changfangti") == "canfanti"
    assert search_module.soft_form("suxing") == "suxin"
    assert search_module.soft_form("shuxing") == "suxin"
