#!/usr/bin/env python3

from typing import Union
import unittest
import itertools
import random
import sys
sys.path.append('..')  # noqa: E402

from oatmeal import OatmealMsg, OatmealParseError


def random_unicode_string(n: int) -> str:
    # unicode values 0..0xd7ff and 0xe000..0x10ffff are valid in utf-8, 16, 32
    # 0xd800..0xdfff are surrogates reserved for use in UTF-16 surrogate pairs,
    # and cannot legally be used outside UTF-16.
    N = 0xd800 + 0x110000 - 0xe000
    x = random.randrange(N)
    x = x if x < 0xd800 else (x - 0xd800) + 0xe000
    return ''.join(chr(x) for _ in range(n))


def random_bytearray(n: int) -> bytearray:
    return bytearray([random.randint(0, 255) for _ in range(n)])


class TestOatmealProtocol(unittest.TestCase):
    def _assert_valid_frame(self, frame: Union[bytes, bytearray]) -> None:
        self.assertTrue(all(b > 0 for b in frame))
        self.assertTrue(all(b != ord('<') for b in frame[1:-3]))
        self.assertTrue(all(b != ord('>') for b in frame[1:-3]))

    def _test_encode_decode(self, msg: OatmealMsg) -> OatmealMsg:
        """ Returns the message after being encoded and decoded """
        frame = msg.encode()
        msg2 = OatmealMsg.decode(frame)
        frame2 = msg2.encode()
        # Test frame is valid and that encoding/decoding worked
        self._assert_valid_frame(frame)
        self.assertEqual(msg, msg2)
        self.assertEqual(msg.args, msg2.args)
        self.assertEqual(frame, frame2)
        return msg2

    def _test_args(self, *args) -> None:
        msg = OatmealMsg("TSTR", *args, token="aa")
        msg2 = self._test_encode_decode(msg)
        self.assertEqual(msg2.args, list(args))

    def test_properties(self) -> None:
        msg = OatmealMsg("TSTR", 1, "abc", [], token="xy")
        self.assertEqual(msg.opcode, "TSTR")
        self.assertEqual(msg.command, "TST")
        self.assertEqual(msg.flag, "R")
        self.assertEqual(msg.token, "xy")
        self.assertEqual(msg.args, [1, "abc", []])
        self.assertEqual(msg.checksums, "SM")

    def test_comparison(self) -> None:
        msgs = [OatmealMsg("TSTR", token="aa"),
                OatmealMsg("TSTR", "arg", token="aa"),
                OatmealMsg("TSTR", token="ab"),
                OatmealMsg("TSTA", token="aa")]
        # Assert that no pair of OatmealMsg from msgs is equal
        for a, b in itertools.combinations(msgs, 2):
            self.assertNotEqual(a, b)
        # Assert that each message is equal to itself and a clone of itself
        for x in msgs:
            self.assertEqual(x, x)
            self.assertEqual(x, OatmealMsg(x.opcode, *x.args, token=x.token))
        # Assert that there is a total ordering over all msgs
        sorted_msgs = sorted(msgs)
        for i in range(len(sorted_msgs)-1):
            self.assertLess(sorted_msgs[i], sorted_msgs[i+1])

    def test_encode_decode_mixed(self) -> None:
        self._test_args(1, ["a", -1.2], [], 0, ["adsf", "asdf"])  # type:ignore

    def test_encode_decode_empty(self) -> None:
        self._test_args()

    def test_encode_decode_nested(self) -> None:
        self._test_args([[], [1, 2, ["x"]], []], 101)

    def test_encode_decode_none(self) -> None:
        self._test_args([], None, [None, "x", None], 1)  # type: tuple

    def test_args_empty_str(self) -> None:
        self._test_args("")
        self._test_args("a", "", "b")
        self._test_args("", "b")
        self._test_args("a", "")
        self._test_args("", "", "")
        self._test_args([], "", ["", [""]],
                        "ßVą😾😏Z😮⚒ª®ğ⚇😜♻Əŵ{úƺ😑ȈœŢƃĨ☿",
                        bytes([]), [0])

    def test_unicode_strs(self) -> None:
        self._test_args("Y¥😃⚈­Ę☇űĘŊßÈŹ♙ýĢzc☥ķüîɂ¼=Ȏ",
                        "",
                        "ńĜ☌B¨⚕😮i7ļòŶ♓♎§ßȃ☙♩😹[·¿Ŧǚ♻")
        self._test_args("Y¥😃⚈­Ę☇űĘŊßÈŹ♙ýĢzc☥ķüîɂ¼=Ȏ",)
        self._test_args("ßVą😾😏Z😮⚒ª®ğ⚇😜♻Əŵ{úƺ😑ȈœŢƃĨ☿",)
        self._test_args("ńĜ☌B¨⚕😮i7ļòŶ♓♎§ßȃ☙♩😹[·¿Ŧǚ♻",)

        # Test 100 random unicode strings
        for i in range(100):
            self._test_args(random_unicode_string(random.randrange(50)))

    @unittest.skip("Skipping slow test")
    def test_all_unicode_codepoints(self) -> None:
        """ Test we can reliably encode+decode all valid unicode values
        unicode values 0..0xd7ff and 0xdfff..0x10ffff are valid in
        utf-8, 16, 32 """
        for i in itertools.chain(range(0xd800), range(0xe000, 0x110000)):
            self._test_args(chr(i))

    def test_str_args(self) -> None:
        # Test some weird strings
        self._test_args("102")
        self._test_args("a[b,d]")
        self._test_args("\n\r\n")
        self._test_args("H\\i!")
        self._test_args("<>\"'\\,a")
        self._test_args("Hi!")
        self._test_args("\\\\\\")
        self._test_args("\\u2")

    def test_raw_bytes(self) -> None:
        # Test data encoding
        self._test_args(random_bytearray(0))
        self._test_args(random_bytearray(1))
        self._test_args(random_bytearray(2))
        self._test_args(random_bytearray(3))
        self._test_args(random_bytearray(50))
        self._test_args(random_bytearray(100))

        # Decode, then encode and compare
        long0 = b'<HRTBVU{a=5.1,avail_kb=247,b="hi",loop_ms=1,uptime=16}>BH'
        long1 = b'<HRTB0E{Itotal=0.372172,v1=F,v10=F,v2=F,v3=F,v4=F,v5=F,v6=F,v7=F,v8=F,v9=F}>yI'
        long2 = b'<DISAea"ValveCluster",0,"0031FFFFFFFFFFFF4E45356740010017","e5938cd">Hg'
        self.assertEqual(OatmealMsg.decode(bytearray(long0)).encode(), long0)
        self.assertEqual(OatmealMsg.decode(bytearray(long1)).encode(), long1)
        self.assertEqual(OatmealMsg.decode(bytearray(long2)).encode(), long2)

    def _test_parse_args(self, args: str, expected_res: list) -> None:
        """ Parse args and test that we get a given result """
        parsed_args = OatmealMsg._parse_args(args.encode('ascii'))
        self.assertEqual(parsed_args, expected_res)

    def _test_parse_args_fails(self, args: str) -> None:
        """ Test that parsing the given arg string raises an exception """
        with self.assertRaises(OatmealParseError):
            OatmealMsg._parse_args(args.encode('ascii'))

    def test_arg_decoding(self) -> None:
        """ Test that we can decode bytes into python objects """
        self._test_parse_args("1,2", [1, 2])
        self._test_parse_args("", [])
        self._test_parse_args("[1,2]", [[1, 2]])
        self._test_parse_args("[[]]", [[[]]])
        self._test_parse_args("[hi],bye", [["hi"], "bye"])
        self._test_parse_args("1,2,[3,4,asdf],N,T,F",
                              [1, 2, [3, 4, "asdf"], None, True, False])
        self._test_parse_args("1,2,[[],[],3,[]],N,T,F",
                              [1, 2, [[], [], 3, []], None, True, False])
        # Test that an exception is raised for invalid arg lists
        self._test_parse_args_fails("[")
        self._test_parse_args_fails("]")
        self._test_parse_args_fails("1,")
        self._test_parse_args_fails("[,2]")
        self._test_parse_args_fails("[4,5,]")
        self._test_parse_args_fails("[1,2]]")
        self._test_parse_args_fails("[[1,2]")
        self._test_parse_args_fails("1,,3")
        self._test_parse_args_fails("[1]3")
        self._test_parse_args_fails("[1][2]")
        self._test_parse_args_fails(",]")
        self._test_parse_args_fails("[]]")
        self._test_parse_args_fails(",")
        self._test_parse_args_fails("{")
        self._test_parse_args_fails("}")
        self._test_parse_args_fails("{123}")
        self._test_parse_args_fails("{a=1,1}")
        self._test_parse_args_fails("{a=1,b=2,}")
        self._test_parse_args_fails("{},")
        self._test_parse_args_fails("{,a=1}")
        self._test_parse_args_fails(",{a=1}")
        self._test_parse_args_fails("{\"a\"=1}")

    def test_dicts(self) -> None:
        # Test empty and nested dicts
        msg0 = OatmealMsg("TSTR", {}, token='XY')
        msg1 = OatmealMsg("TSTR", "", {}, [], token='aa')
        msg2 = OatmealMsg("TSTR", "", {'a': {'b': {}}, 'c': {}}, [], token='XY')

        # Test different data types in dicts
        msg3 = OatmealMsg("XYZA", 1, 2,
                          {'int': -1, 'float': 1.2, 'bool': True,
                           'str': "asdf", 'bytes': b'123',
                           'list': [1, 2, "hi"], 'none': None}, None,
                          token='zZ')

        self.assertEqual(OatmealMsg.decode(msg0.encode()), msg0)
        self.assertEqual(OatmealMsg.decode(msg1.encode()), msg1)
        self.assertEqual(OatmealMsg.decode(msg2.encode()), msg2)
        self.assertEqual(OatmealMsg.decode(msg3.encode()), msg3)

    def test_checksum(self) -> None:
        """ Test checksum values match those we're pre-computed. """
        msg0 = OatmealMsg("DISR", token='XY')
        msg1 = OatmealMsg("RUNR", 1.23, True, "Hi!", [1, 2], token='aa')
        msg2 = OatmealMsg("XYZA", 101, [0, 42], token='zZ')
        msg3 = OatmealMsg("LOLR", 123, True, 99.9, token='Oh')
        self.assertEqual(msg0.encode(), b'<DISRXY>i_')
        self.assertEqual(msg1.encode(), b'<RUNRaa1.23,T,"Hi!",[1,2]>-b')
        self.assertEqual(msg2.encode(), b'<XYZAzZ101,[0,42]>SH')
        self.assertEqual(msg3.encode(), b'<LOLROh123,T,99.9>SS')

    def test_repr(self) -> None:
        """
        Confirm that OatmealMsg's __repr__ returns a representation in the
        format we expect.
        """
        msg0_str = "OatmealMsg('DISR', token='XY')"
        msg1_str = "OatmealMsg('RUNR', 1.23, True, 'Hi!', [1, 2], token='aa')"
        msg2_str = "OatmealMsg('XYZA', 101, [0, 42], token='zZ')"
        msg3_str = "OatmealMsg('LOLR', 123, True, 99.9, token='Oh')"
        msg4_str = "OatmealMsg('TSTR', 1, 'abc', [], token='xy')"
        msg5_str = "OatmealMsg('QWER', '', token='AZ')"
        self.assertEqual(repr(eval(msg0_str)), msg0_str)
        self.assertEqual(repr(eval(msg1_str)), msg1_str)
        self.assertEqual(repr(eval(msg2_str)), msg2_str)
        self.assertEqual(repr(eval(msg3_str)), msg3_str)
        self.assertEqual(repr(eval(msg4_str)), msg4_str)
        self.assertEqual(repr(eval(msg5_str)), msg5_str)


if __name__ == '__main__':
    unittest.main()
