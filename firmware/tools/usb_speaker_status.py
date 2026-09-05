#!/usr/bin/env python3
"""Read USB playback counters, or explicitly restore the previous valid firmware."""
import argparse
import ctypes as c
import ctypes.util
import json
import struct


def read_status(restore_previous=False):
    library = ctypes.util.find_library('usb-1.0') or '/opt/homebrew/lib/libusb-1.0.dylib'
    usb = c.CDLL(library)
    ptr = c.c_void_p
    usb.libusb_init.argtypes = [c.POINTER(ptr)]
    usb.libusb_init.restype = c.c_int
    usb.libusb_exit.argtypes = [ptr]
    usb.libusb_open_device_with_vid_pid.argtypes = [ptr, c.c_uint16, c.c_uint16]
    usb.libusb_open_device_with_vid_pid.restype = ptr
    usb.libusb_close.argtypes = [ptr]
    usb.libusb_get_string_descriptor_ascii.argtypes = [ptr, c.c_uint8, ptr, c.c_int]
    usb.libusb_control_transfer.argtypes = [ptr, c.c_uint8, c.c_uint8, c.c_uint16,
                                           c.c_uint16, ptr, c.c_uint16, c.c_uint]
    usb.libusb_control_transfer.restype = c.c_int
    context = ptr()
    if usb.libusb_init(c.byref(context)) != 0:
        raise RuntimeError('Cannot initialize USB library')
    handle = None
    try:
        handle = usb.libusb_open_device_with_vid_pid(context, 0x303a, 0x8000)
        if not handle:
            raise RuntimeError('PonChan USB Speaker is not connected or cannot be opened')
        name = c.create_string_buffer(256)
        size = usb.libusb_get_string_descriptor_ascii(handle, 2, name, 256)
        if size < 0 or name.value != b'PonChan USB Speaker':
            raise RuntimeError('USB product does not match PonChan USB Speaker')
        if restore_previous:
            count = usb.libusb_control_transfer(handle, 0x40, 0x51, 0x5043, 0, None, 0, 2000)
            if count != 0:
                raise RuntimeError(f'USB restore request failed: {count}')
            return {'restore_requested': True, 'verification_required': True}
        buffer = c.create_string_buffer(28)
        count = usb.libusb_control_transfer(handle, 0xc0, 0x50, 0x5043, 0, buffer, 28, 2000)
        if count != 28:
            raise RuntimeError(f'USB status request failed: {count}')
        keys = ['version', 'write_calls', 'frames_received', 'frames_written',
                'nonzero_writes', 'write_errors', 'frames_yielded_to_native']
        result = dict(zip(keys, struct.unpack('<7I', buffer.raw)))
        if result['version'] != 1:
            raise RuntimeError('Unsupported diagnostics version')
        boot = c.create_string_buffer(8)
        boot_count = usb.libusb_control_transfer(handle, 0xc0, 0x52, 0x5043, 0, boot, 8, 2000)
        if boot_count == 8:
            subtype, state = struct.unpack('<2I', boot.raw)
            result['ota_slot'] = subtype - 0x10
            result['ota_state'] = state
            result['boot_confirmed'] = state == 2
        return result
    finally:
        if handle:
            usb.libusb_close(handle)
        usb.libusb_exit(context)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--restore-previous-firmware', action='store_true',
                        help='Explicitly request rollback and reboot; requires a valid previous OTA slot')
    args = parser.parse_args()
    try:
        print(json.dumps(read_status(args.restore_previous_firmware), indent=2))
    except (RuntimeError, OSError) as error:
        raise SystemExit(str(error))
