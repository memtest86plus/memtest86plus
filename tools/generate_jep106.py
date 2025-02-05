#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0
"""
Tool for parsing JEP106 pdf file from JDEC allowing to extract ID and vendor
string data and generate / update jep106.S file.

Example:
  ./generate_jep106.py JEP106BK.pdf ../system/jep106.S
"""

import re
import subprocess
import sys
import os

NUMS = {
  'one': 1,
  'two': 2,
  'three': 3,
  'four': 4,
  'five': 5,
  'six': 6,
  'seven': 7,
  'eight': 8,
  'nine': 9,
  'ten': 10,
  'eleven': 11,
  'twelve': 12,
  'thirteen': 13,
  'fourteen': 14,
  'fifteen': 15,
}

MONTHS = {
  'january': 1,
  'february': 2,
  'march': 3,
  'april': 4,
  'may': 5,
  'june': 6,
  'july': 7,
  'august': 8,
  'september': 9,
  'october': 10,
  'november': 11,
  'december': 12
}

RE_SCOPE_DATE = re.compile(r'The present list is complete as of ([A-Za-z]+) ([0-9]+), ([0-9]+)')
RE_BANK = re.compile(r'The following numbers are all in bank (.+):')
RE_COMPANY = re.compile(r'(\d+) (.+) ([01] ){8}([0-9A-F]{2})$')
RE_MI_CODE = re.compile(r'([01] ){8}([0-9A-F]{2})$')
RE_JEP106 = re.compile(r'\s+jedec\s"([0-9A-F]{4})".*"(.+)"$')

def parse_pdf(file):
  result = subprocess.run(['pdftotext', '-raw', '-enc', 'Latin1', file, '/dev/stdout'],
                          stdout=subprocess.PIPE)

  data = []
  bank = 1
  company = False
  mode = None

  file_id = None
  date_id = None
  scope_txt = ''
  mic = ''

  data.append((0, 0, 'Noname'))

  for line in result.stdout.splitlines():
    line = line.decode('latin-1');

    if line.startswith('Annex'):
      break

    if mode is None and line == 'Standard Manufacturer\'s':
      mode = 'sm'
      continue

    if mode == 'sm' and line == 'Identification Code':
      mode = 'ic'
      continue

    if mode == 'ic':
      file_id = line
      mode = 'ic-done'
      continue

    if line == '2 Scope':
      mode = 'scope'
      continue

    if mode == 'scope':
      scope_txt = scope_txt + ' ' + line
      date_match = RE_SCOPE_DATE.search(scope_txt)
      if date_match:
        month = date_match.group(1).lower()
        day = int(date_match.group(2))
        if month in MONTHS:
          date_id = '%s-%02d-%02d' % (date_match.group(3), MONTHS[month], day)

        scope_txt = ''
        mode = 'need-company'
      continue

    new_bank = RE_BANK.search(line)
    if new_bank:
      bank_text = new_bank.group(1)

      if bank_text in NUMS:
        bank = NUMS[bank_text]
      else:
        bank = int(bank_text)

      continue

    if line == 'COMPANY 8 7 6 5 4 3 2 1 HEX':
      mode = 'company'
      continue

    if mode not in ('company', 'mic'):
      continue

    # Note: long names may be split between multiple lines
    mi_code = RE_MI_CODE.search(line)
    if mi_code:
      mic += ' ' + line

      new_company = RE_COMPANY.search(mic)

      mic = ''
      mode = 'company'

      if new_company:
        company_id = int(new_company.group(1))
        company_name = new_company.group(2)

        data.append((bank - 1, company_id, company_name))

      continue

    if mode == 'mic':
      mic += ' ' + line
      continue

    words = line.split(' ')
    if words[0].isdigit():
      mic = line
      mode = 'mic'
      continue

  data.append((255, 255, 'Unknown'))

  return (file_id, date_id, data)

def parse_jep106(jep106_file):

  current_data = {}

  try:
    with open(jep106_file) as result:
      for line in result:
        line = line.rstrip('\n')

        jep106_match = RE_JEP106.search(line)
        if jep106_match:

          current_data[jep106_match.group(1)] = {
            'name': jep106_match.group(2),
            'enabled': not line.startswith('//'),
            }
  except:
    pass

  return current_data

def main(argv):

  current_data = {}

  if len(argv) < 3:
    print('Syntax:', file=sys.stderr)
    print('%s <JEP106.pdf> <jep106.S>' % argv[0], file=sys.stderr)
    return 1

  pdf_file = argv[1]
  if not os.path.isfile(pdf_file):
    print('File "%s" does not exist' % file, file=sys.stderr)
    return 2

  jep106_file = argv[2]
  current_data = parse_jep106(jep106_file)

  (file_id, date_id, jedec_data) = parse_pdf(pdf_file)

  if not jedec_data:
    print("Error parsing JEP106BK file %s" % pdf_file, file=sys.stderr)
    return 3

  if not file_id:
    file_id = 'JEP106'

  if not date_id:
    date_id = 'Unknown'

  jep106_data = []

  for (bank, company_id, company_name) in jedec_data:
    jedec_id = "%02X%02X" % (bank, company_id)

    if current_data:
      enabled = False
    else:
      enabled = True

    if jedec_id in ('0000', 'FFFF'):
      enabled = True

    if jedec_id in current_data:
      if current_data[jedec_id]['enabled']:
        enabled = True

      current_company_name = current_data[jedec_id]['name']
      if company_name != current_company_name:
        if enabled:
          company_name = current_company_name
        else:
          print('%s: "%s" -> "%s"' % (jedec_id, current_company_name, company_name))

    jep106_data.append('%s\tjedec\t"%s", "%s"' % ('' if enabled else '//', jedec_id, company_name))

  with open(jep106_file, 'w') as f:
    f.write('// SPDX-License-Identifier: GPL-2.0\n')
    f.write('/*\n')
    f.write(' * SPD JEDEC Manufacturer codes.\n')
    f.write(' *\n')
    f.write(' * Based on JEDEC %s from %s\n' % (file_id, date_id))
    f.write(' *\n')
    f.write(' * The list has back to back records of the following structure:\n')
    f.write(' * uint8_t len;\n')
    f.write(' * uint8_t code_h\n')
    f.write(' * uint8_t code_l\n')
    f.write(' * char    name[]\n')
    f.write(' *\n')
    f.write(' * `len` is the length of the entire record, including the len field itself.\n')
    f.write(' * `code_h` and `code_l` together form the 16-bit JEDEC manufacturer ID.\n')
    f.write(' * `name` is a null terrminated string\n')
    f.write(' *\n')
    f.write(' * The list is terminated by a single zero byte (len = 0).\n')
    f.write(' * The minimal record size - `len` is 5: 1 byte for len, 2 bytes for code,\n')
    f.write(' * plus a null terrminated string with at least single character (2 bytes).\n')
    f.write(' *\n')
    f.write(' */\n')
    f.write('\n')
    f.write('\t.global\tjep106_data\n')
    f.write('\t.section\t".rodata"\n')
    f.write('\n')
    f.write('\t.macro jedec\tid, name\n')
    f.write('\t.byte\t(.jep106_\\id\\()_end - .jep106_\\id\\()_start + 3)\n')
    f.write('\t.byte\t(0x\\id >> 8) & 0xff, 0x\\id & 0xff\n')
    f.write('\t.jep106_\\id\\()_start:\n')
    f.write('\t.asciz\t"\\name"\n')
    f.write('\t.jep106_\\id\\()_end:\n')
    f.write('\t.endm\n')
    f.write('\n')
    f.write('jep106_data:\n')
    for j in jep106_data:
      f.write('%s\n' % j)
    f.write('\t.byte\t0\n')

if __name__ == '__main__':
  sys.exit(main(sys.argv))
