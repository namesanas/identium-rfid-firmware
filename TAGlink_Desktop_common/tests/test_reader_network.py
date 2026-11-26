import unittest
from unittest.mock import patch

from reader_network import WiredSettings, exchange_over_ethernet


ORIGINAL_REPLY = b'IPGET\xc0\xa8\x01\x32\xff\xff\xff\x00\xc0\xa8\x01\x01\x02\x01\x02\x03\x04\x05\x1f\x90'


class SettingsSocket:
    def __init__(self, *replies):
        self.replies = iter(replies)
        self.sent = []

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    def sendall(self, data):
        self.sent.append(data)

    def recv(self, size):
        return next(self.replies, b'')


class ReaderNetworkTests(unittest.TestCase):
    def setUp(self):
        self.current = WiredSettings.from_reply(ORIGINAL_REPLY)
        self.proposed = self.current.updated('192.168.1.35', '255.255.255.0', '192.168.1.1')

    def test_read_handles_fragmented_reply_and_write_keeps_mac_and_settings_port(self):
        connection = SettingsSocket(ORIGINAL_REPLY[:3], ORIGINAL_REPLY[3:])
        with patch('reader_network.socket.create_connection', return_value=connection):
            self.assertEqual(exchange_over_ethernet('192.168.1.50', 8080, None, None), (self.current, ''))
        self.assertEqual(connection.sent, [b'IPGET'])
        self.assertEqual(self.proposed.save_frame(), b'IPSET\xc0\xa8\x01\x23' + ORIGINAL_REPLY[9:])

    def test_bad_address_or_netmask_is_rejected(self):
        for address, mask in (('192.168.1.256', '255.255.255.0'), ('192.168.1.255', '255.255.255.0'),
                              ('192.168.1.35', '255.0.255.0'), ('192.168.1.35', '0.0.0.255')):
            with self.subTest(address=address, mask=mask), self.assertRaises(ValueError):
                self.current.updated(address, mask, '192.168.1.1')

    def test_saves_then_reads_back_from_new_address(self):
        reply = b'IPGET' + self.proposed.save_frame()[5:]
        old = SettingsSocket(ORIGINAL_REPLY, reply)
        new = SettingsSocket(reply)
        with (patch('reader_network.socket.create_connection', side_effect=[old, new]) as connect,
              patch('reader_network.time.sleep')):
            self.assertEqual(exchange_over_ethernet('192.168.1.50', 8080, self.current, self.proposed),
                             (self.proposed, ''))
        self.assertEqual(old.sent, [b'IPGET', self.proposed.save_frame(), b'IPGET'])
        self.assertEqual(new.sent, [b'IPGET'])
        self.assertEqual([call.args[0] for call in connect.call_args_list],
                         [('192.168.1.50', 8080), ('192.168.1.35', 8080)])

    def test_save_when_computer_cannot_reach_the_new_address(self):
        reply = b'IPGET' + self.proposed.save_frame()[5:]
        old = SettingsSocket(ORIGINAL_REPLY, reply)
        with (patch('reader_network.socket.create_connection', side_effect=[old, TimeoutError(), TimeoutError()]),
              patch('reader_network.time.sleep')):
            saved, message = exchange_over_ethernet('192.168.1.50', 8080, self.current, self.proposed)
        self.assertEqual(saved, self.proposed)
        self.assertIn('Check that your computer can reach the new address', message)

    def test_save_refuses_a_different_reader_on_the_original_address(self):
        other = SettingsSocket(ORIGINAL_REPLY[:17] + b'\x02\x06' + ORIGINAL_REPLY[19:])
        with patch('reader_network.socket.create_connection', return_value=other), self.assertRaises(ValueError):
            exchange_over_ethernet('192.168.1.50', 8080, self.current, self.proposed)
        self.assertEqual(other.sent, [b'IPGET'])
