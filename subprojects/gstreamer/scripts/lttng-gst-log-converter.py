#!/usr/bin/env python3
#
# lttng-gst-log-converter.py: Converts LTTng GStreamer debug logs in CTF to a GStreamer-like log format.
#
# Copyright (C) 2025 ekwange <ekwange@gmail.com>
#
# This script is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This script is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public
# License along with this script; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

import re
import sys
import logging
import os

# Configure logging
logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s', stream=sys.stderr)

LOG_LEVELS = {
    1: "ERROR  ", 2: "WARN   ", 3: "FIXME  ", 4: "INFO   ", 5: "DEBUG  ",
    6: "LOG    ", 7: "TRACE  ", 9: "MEMDUMP"
}

line_re = re.compile(
    r'\[(?P<ts_h>\d{2}):(?P<ts_m>\d{2}):(?P<ts_s>\d{2})\.(?P<ts_ns>\d{9})\] '
    r'.*? gst_lttng:gst_log: '
    r'\{ cpu_id = \d+ \}, '
    r'\{ (?P<payload>.*) \}'
)

def parse_payload(payload_str):
    payload = {}
    try:
        # The message is the last part and can contain commas.
        # Find the last 'message = "'
        msg_start_index = payload_str.rfind('message = "')
        if msg_start_index == -1:
            # No message found, parse the whole string
            parts_str = payload_str
            payload['message'] = ''
        else:
            payload['message'] = payload_str[msg_start_index + 11:-1].replace("\\'", "'")
            parts_str = payload_str[:msg_start_index]

        # Use findall for safer parsing of key-value pairs
        # This regex finds key = "quoted value" or key = unquoted_value
        kv_pairs = re.findall(r'(\w+)\s*=\s*(?:"([^"]*)"|([^\s,]+))', parts_str)

        for key, quoted_val, unquoted_val in kv_pairs:
            # The value is either in the quoted group or the unquoted one.
            # The unquoted value might have a trailing comma if it's the last one, so strip it.
            value = quoted_val if quoted_val else unquoted_val.rstrip(',')
            payload[key] = value

    except Exception as e:
        logging.error(f"Error parsing payload: {e} on '{payload_str}'")
        return None
    return payload

def main():
    logging.info("Starting script")
    if len(sys.argv) != 3:
        logging.error(f"Usage: {sys.argv[0]} <input_file> <output_file>")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    logging.info(f"Input file: {input_path}")
    logging.info(f"Output file: {output_path}")

    base_ns = None
    line_count = 0

    with open(input_path, 'r', encoding='latin-1') as infile, open(output_path, 'w') as outfile:
        for line in infile:
            line_count += 1
            # if line_count % 1000 == 0:
            #     logging.info(f"Processing line {line_count}")

            match = line_re.match(line)
            if not match:
                logging.warning(f"Line {line_count} does not match regex: {line.strip()}")
                continue

            data = match.groupdict()

            ts_ns = (int(data['ts_h']) * 3600 * 10**9 +
                     int(data['ts_m']) * 60 * 10**9 +
                     int(data['ts_s']) * 10**9 +
                     int(data['ts_ns']))

            if base_ns is None:
                base_ns = ts_ns
                logging.info(f"Base timestamp set to: {base_ns}")

            delta_ns = ts_ns - base_ns

            h = delta_ns // (3600 * 10**9)
            m = (delta_ns % (3600 * 10**9)) // (60 * 10**9)
            s = (delta_ns % (60 * 10**9)) // 10**9
            ns = delta_ns % 10**9

            timestamp = f"{h}:{m:02d}:{s:02d}.{ns:09d}"

            payload = parse_payload(data['payload'])
            if not payload:
                logging.warning(f"Failed to parse payload on line {line_count}: {data['payload']}")
                continue

            if line_count < 5: # Log first few payloads
                logging.debug(f"Parsed payload on line {line_count}: {payload}")

            pid_val = payload.get('pid') or payload.get('vpid', '0')
            pid = pid_val.ljust(7)
            try:
                tid_hex = f"0x{int(payload.get('vtid', 0)):x}"
            except (ValueError, TypeError):
                tid_hex = "0x0"
            tid = tid_hex.ljust(14)

            level_val = int(payload.get('level', 0))
            level = LOG_LEVELS.get(level_val, "UNKNOWN")
            category = payload.get('category', '')

            # level is already padded to 7 chars from LOG_LEVELS
            # category is padded to 20 chars, with 1 space in between
            level_cat_part = f"{level} {category.ljust(20)}"

            file = os.path.basename(payload.get('file', ''))
            line_num = payload.get('line', '')
            function = payload.get('function', '')

            object_name_from_payload = payload.get('object', '')
            object_ptr_from_payload = payload.get('object_ptr', '')
            message = payload.get('message', '')

            final_obj_prefix = ""

            # Extract object name if present and remove <>
            extracted_name = ""
            if object_name_from_payload.startswith('<') and object_name_from_payload.endswith('>'):
                extracted_name = object_name_from_payload[1:-1]
            elif object_name_from_payload: # if it's not wrapped in <> but still has a value
                extracted_name = object_name_from_payload

            # Check if object_ptr is meaningful
            is_valid_object_ptr = (object_ptr_from_payload and object_ptr_from_payload != '0x0')

            if extracted_name and is_valid_object_ptr:
                final_obj_prefix = f"<{extracted_name}:{object_ptr_from_payload}>"
            elif extracted_name:
                final_obj_prefix = f"<{extracted_name}>"
            elif is_valid_object_ptr:
                final_obj_prefix = f"<{object_ptr_from_payload}>"

            if final_obj_prefix:
                message = f"{final_obj_prefix} {message}"

            log_line = (
                f"{timestamp} {pid} {tid} {level_cat_part} "
                f"{file}:{line_num}:{function}: {message}\n"
            )

            outfile.write(log_line)

    logging.info(f"Finished writing to {output_path}.")
    logging.info(f"Script finished. Processed {line_count} lines.")

if __name__ == "__main__":
    main()
