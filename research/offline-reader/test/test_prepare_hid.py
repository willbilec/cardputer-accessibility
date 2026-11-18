from pathlib import Path
import importlib.util
import re
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('prepare_hid',ROOT/'prepare_hid.py')
adapt=importlib.util.module_from_spec(spec);spec.loader.exec_module(adapt)
class TransportTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):adapt.prepare()
 def test_descriptor_input_sizes(self):
  header=(ROOT/'generated/bluetooth_generated_api.h').read_text()
  descriptor=header.split('HID_REPORT_MAP[] = {',1)[1].split('};',1)[0]
  descriptor=re.sub(r'//[^\n]*','',descriptor)
  data=bytes(int(n,16) for n in re.findall(r'0x([0-9A-Fa-f]+)',descriptor))
  report=0;size=0;count=0;bits={};i=0
  while i<len(data):
   prefix=data[i];i+=1;n=(0,1,2,4)[prefix&3]
   value=int.from_bytes(data[i:i+n],'little');i+=n
   kind=(prefix>>2)&3;tag=prefix>>4
   if kind==1 and tag==7:size=value
   if kind==1 and tag==9:count=value
   if kind==1 and tag==8:report=value
   if kind==0 and tag in (8,9):bits[report,tag]=bits.get((report,tag),0)+size*count
  self.assertEqual(bits[(1,8)],32)
  self.assertEqual(bits[(2,8)],64)
  self.assertEqual(bits[(2,9)],8)
 def test_pinned_source_fail_closed(self):
  for name,sha in adapt.HASHES.items():self.assertEqual(len(sha),64,name)
  self.assertIn('std::atomic<bool>',(ROOT/'generated/hid_transport_generated.h').read_text())
 def test_no_graphics_dependency(self):
  source=(ROOT/'generated/bluetooth_generated_api.h').read_text()+(ROOT/'generated/usb_generated_api.h').read_text()
  self.assertNotIn('M5Cardputer.h',source);self.assertNotIn('display.h',source)
if __name__=='__main__':unittest.main()
