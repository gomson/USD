#!/pxrpythonsubst
#
# Copyright 2017 Pixar
#
# Licensed under the Apache License, Version 2.0 (the "Apache License")
# with the following modification; you may not use this file except in
# compliance with the Apache License and the following modification to it:
# Section 6. Trademarks. is deleted and replaced with:
#
# 6. Trademarks. This License does not grant permission to use the trade
#    names, trademarks, service marks, or product names of the Licensor
#    and its affiliates, except as required to comply with Section 4(c) of
#    the License and to reproduce the content of the NOTICE file.
#
# You may obtain a copy of the Apache License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the Apache License with the above modification is
# distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the Apache License for the specific
# language governing permissions and limitations under the Apache License.

import sys, os, unittest
from pxr import Sdf,Usd,Vt,Tf, Gf

class TestUsdTimeSamples(unittest.TestCase):
    def test_Basic(self):
        """Sanity tests for Usd.TimeCode API"""
        default1 = Usd.TimeCode.Default()
        default2 = Usd.TimeCode.Default()
        self.assertEqual(default1, default2)
        earliestTime1 = Usd.TimeCode.EarliestTime()
        earliestTime2 = Usd.TimeCode.EarliestTime()
        self.assertEqual(earliestTime1, earliestTime2)
        self.assertEqual(default1, Usd.TimeCode(default1))

        nonSpecial = Usd.TimeCode(24.0)
        self.assertNotEqual(default1, nonSpecial)
        self.assertNotEqual(earliestTime1, nonSpecial)
        print default1, default2, nonSpecial, earliestTime1, earliestTime2

        # test relational operators and hash.
        time1 = Usd.TimeCode(1.0)
        time2 = Usd.TimeCode(2.0)
        self.assertTrue(time1 == Usd.TimeCode(1.0))
        self.assertTrue(time2 == Usd.TimeCode(2.0))
        self.assertTrue(time1 != Usd.TimeCode(2.0))
        self.assertTrue(time2 != Usd.TimeCode(1.0))
        self.assertTrue(time1 != time2)
        self.assertTrue(time1 < time2)
        self.assertTrue(time1 <= time2)
        self.assertTrue(time2 > time1)
        self.assertTrue(time2 >= time1)
        self.assertTrue(not (time1 < time1))
        self.assertTrue(time1 <= time1)
        self.assertTrue(not (time1 > time1))
        self.assertTrue(time1 >= time1)
        self.assertTrue(default1 < time1)
        self.assertTrue(default1 <= time1)
        self.assertTrue(time1 > default1)
        self.assertTrue(time1 >= default1)
        self.assertTrue(default1 < earliestTime1)
        self.assertTrue(default1 <= earliestTime1)
        self.assertTrue(time1 > earliestTime1)
        self.assertTrue(time1 >= earliestTime1)
        self.assertTrue(hash(default1) == hash(default2))
        self.assertTrue(hash(earliestTime1) == hash(earliestTime2))
        self.assertTrue(hash(default1) != hash(earliestTime1))
        self.assertTrue(hash(Usd.TimeCode(1.234)) == hash(Usd.TimeCode(1.234)))
        self.assertTrue(hash(time1) != hash(time2))

        # Basic tests for SafeStep.
        d = Usd.TimeCode.SafeStep()
        self.assertTrue(Usd.TimeCode(1e6+d) != 1e6 and Usd.TimeCode(1e6+d) > 1e6)
        self.assertTrue(Usd.TimeCode(1e12+d) == 1e12) # <- aliases at this scale
        d = Usd.TimeCode.SafeStep(maxValue=1e12)
        self.assertTrue(Usd.TimeCode(1e12+d) != 1e12)

        # with our factor of 2 safety margin, two values separated by delta at twice
        # the max, scaled down by the max scale factor, then shifted back out to the
        # max (not scaled) should still be distinct.
        d = Usd.TimeCode.SafeStep()
        t1, t2 = (1e6*2)/10.0, (1e6*2+d)/10.0
        self.assertTrue(t1 != t2 and t1 < t2)
        # shift them over so they're back at twice the max.
        self.assertTrue((t1 + 1800000.0) != (t2 + 1800000.0) and
                (t1 + 1800000.0) < (t2 + 1800000.0))

        # do same test but instead of twice the max, test twice the shrinkage.
        d = Usd.TimeCode.SafeStep()
        t1, t2 = (1e6)/20.0, (1e6+d)/20.0
        self.assertTrue(t1 != t2 and t1 < t2)
        # shift them over so they're back at twice the max.
        self.assertTrue((t1 + 950000.0) != (t2 + 950000.0) and
                (t1 + 950000.0) < (t2 + 950000.0))

        # Assert that invoking GetValue() on Default time raises.
        # with self.assertRaises(RuntimeError):
        #     Usd.TimeCode.Default().GetValue()

        allFormats = ['usd' + x for x in 'ac']

        for fmt in allFormats:

            layerName = "testUsdTimeSamples." + fmt

            if os.path.exists(layerName):
                os.unlink(layerName)

            stage = Usd.Stage.CreateNew(layerName)
            l = stage.GetRootLayer()
            prim = stage.OverridePrim("/Test")
            attr = prim.CreateAttribute("varying", Sdf.ValueTypeNames.Int)
            attr.Set(0)
            attr.Set(1, 1)
            attr.Set(2, 2)
            sdVaryingAttr = l.GetAttributeAtPath(attr.GetPath())
            self.assertEqual(l.ListTimeSamplesForPath(attr.GetPath()), [1.0,2.0])
            self.assertEqual(attr.GetTimeSamples(), [1.0,2.0])
            self.assertEqual(attr.GetTimeSamplesInInterval(Gf.Interval(0,1)), [1.0]) 
            self.assertEqual(attr.GetTimeSamplesInInterval(Gf.Interval(0,6)), [1.0, 2.0])
            self.assertEqual(attr.GetTimeSamplesInInterval(Gf.Interval(0,0)), []) 
            self.assertEqual(attr.GetTimeSamplesInInterval(Gf.Interval(1.0, 2.0)), [1.0, 2.0])  

            # ensure that open, finite endpoints are disallowed
            with self.assertRaises(Tf.ErrorException):
                # IsClosed() will be True by default in the Interval ctor
                bothClosed = Gf.Interval(1.0, 2.0, False, False)
                attr.GetTimeSamplesInInterval(bothClosed)
             
            with self.assertRaises(Tf.ErrorException):
                finiteMinClosed = Gf.Interval(1.0, 2.0, True, False)
                attr.GetTimeSamplesInInterval(finiteMinClosed)
            
            with self.assertRaises(Tf.ErrorException):
                finiteMaxClosed = Gf.Interval(1.0, 2.0, False, True)
                attr.GetTimeSamplesInInterval(finiteMaxClosed)

            self.assertEqual(attr.GetBracketingTimeSamples(1.5), (1.0,2.0))
            self.assertEqual(attr.GetBracketingTimeSamples(1.0), (1.0,1.0))
            self.assertEqual(attr.GetBracketingTimeSamples(2.0), (2.0,2.0))
            self.assertEqual(attr.GetBracketingTimeSamples(.9), (1.0,1.0))
            self.assertEqual(attr.GetBracketingTimeSamples(earliestTime1.GetValue()), 
                        (1.0,1.0))
            self.assertEqual(attr.GetBracketingTimeSamples(2.1), (2.0,2.0))
            # XXX: I would like to verify timeSamples here using the Sd API
            #      but GetInfo fails to convert the SdTimeSampleMap back to
            #      python correctly, and SetInfo does not convert a python
            #      dictionary back to C++ correctly.
            #d = sdVaryingAttr.GetInfo("timeSamples")
            #d[1.0] = 99
            #d[2.0] = 42 
            #sdVaryingAttr.SetInfo("timeSamples", d)
            #self.assertEqual(l.ListTimeSamplesForPath(attr.GetPath()), [99.0,42.0])

            attr = prim.CreateAttribute("unvarying", Sdf.ValueTypeNames.Int)
            attr.Set(0)
            sdUnvaryingAttr = l.GetAttributeAtPath(attr.GetPath())
            self.assertEqual(l.ListTimeSamplesForPath(attr.GetPath()), [])
            self.assertEqual(attr.GetTimeSamples(), [])
            self.assertEqual(attr.GetTimeSamplesInInterval(
                Gf.Interval.GetFullInterval()), [])
            self.assertEqual(attr.GetBracketingTimeSamples(1.5), ())

            # Test for bug/81006 . Could break this out into a separate test, but
            # given the ratio of setup to test, figured I'd stick it in here.  This
            # will segfault if the fix is really not working.
            empty = Vt.DoubleArray()
            emptyAttr = prim.CreateAttribute(
                "empty", Sdf.ValueTypeNames.DoubleArray)
            emptyAttr.Set(empty)
            roundEmpty = emptyAttr.Get(Usd.TimeCode.Default())
            # See bug/81998 why we cannot test for equality here
            self.assertEqual(len(roundEmpty), len(empty))

            # print the layer contents for debugging
            print l.ExportToString()

            self.assertEqual(sdUnvaryingAttr.HasInfo("timeSamples"), False)
            self.assertEqual(sdVaryingAttr.HasInfo("timeSamples"), True)

if __name__ == "__main__":
    unittest.main()
