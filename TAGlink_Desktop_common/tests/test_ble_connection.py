"""Connection/cancellation regressions with simulated Windows Qt callbacks."""
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from PySide6.QtCore import QCoreApplication
from PySide6.QtBluetooth import (QBluetoothAddress, QBluetoothLocalDevice,
                                QLowEnergyCharacteristic, QLowEnergyService)

from ble_transport import BleTransport

APP = QCoreApplication.instance() or QCoreApplication([])


class ConnectionTests(unittest.TestCase):
    def setUp(self):
        self.ble = BleTransport(diagnostics=Mock(path=None, warning=None))
        self.info = Mock()
        self.info.name.return_value = "Test MUBR"
        self.info.address.return_value = QBluetoothAddress("01:02:03:04:05:06")
        self.ble.devices["reader"] = self.info
        self.local = Mock()
        self.local.isValid.return_value = True
        self.local.name.return_value = "Test PC"
        self.local.hostMode.return_value = QBluetoothLocalDevice.HostConnectable
        self.local.pairingStatus.return_value = QBluetoothLocalDevice.Unpaired
        self.factory = Mock(return_value=self.local)
        self.factory.Unpaired = QBluetoothLocalDevice.Unpaired
        self.factory.Paired = QBluetoothLocalDevice.Paired
        self.factory.HostPoweredOff = QBluetoothLocalDevice.HostPoweredOff
        self.controller = Mock()
        self.controller.mtu.return_value = 247
        self.controller.remoteName.return_value = "Test MUBR"
        self.central = patch("ble_transport.QLowEnergyController.createCentral",
                             return_value=self.controller).start()
        self.addCleanup(patch.stopall)
        patch("ble_transport.QBluetoothLocalDevice", self.factory).start()

    def tearDown(self):
        self.ble.close()

    def test_cancel_inside_windows_pairing_status_does_not_request_pairing(self):
        # Qt's Windows pairingStatus() spins the event loop while awaiting WinRT.
        def cancelled(address):
            self.ble.disconnect()
            return QBluetoothLocalDevice.Unpaired

        self.local.pairingStatus.side_effect = cancelled
        self.ble.connect("reader")
        self.local.requestPairing.assert_not_called()
        self.central.assert_not_called()
        self.assertIsNone(self.ble.local)
        self.assertFalse(self.ble.connect_timer.isActive())

    def test_cancel_inside_windows_adapter_construction_releases_returned_adapter(self):
        def cancelled(parent):
            self.ble.disconnect()
            return self.local

        self.factory.side_effect = cancelled
        self.ble.connect("reader")
        self.local.pairingStatus.assert_not_called()
        self.local.requestPairing.assert_not_called()
        self.central.assert_not_called()
        self.local.deleteLater.assert_called_once()
        self.assertIsNone(self.ble.local)

    def test_duplicate_pairing_completion_does_not_replace_live_controller(self):
        self.ble.connect("reader")
        generation = self.ble.generation
        self.ble._paired(self.info, QBluetoothLocalDevice.Paired, generation)
        self.ble._paired(self.info, QBluetoothLocalDevice.Paired, generation)
        self.central.assert_called_once()
        self.assertIs(self.ble.controller, self.controller)

    def test_already_paired_reader_connects_without_another_pairing_request(self):
        self.local.pairingStatus.return_value = QBluetoothLocalDevice.Paired
        self.ble.connect("reader")
        self.local.requestPairing.assert_not_called()
        self.central.assert_called_once()
        self.controller.connectToDevice.assert_called_once()
        self.assertFalse(self.ble.ready)

    def subscribed(self):
        self.ble.connect("reader")
        paired = self.local.pairingFinished.connect.call_args.args[0]
        paired(self.info.address(), QBluetoothLocalDevice.Paired)
        self.controller.connected.connect.call_args.args[0]()
        self.controller.discoverServices.assert_called_once()
        service = self.controller.createServiceObject.return_value
        rx, tx = Mock(), Mock()
        rx.properties.return_value = QLowEnergyCharacteristic.Write
        tx.properties.return_value = QLowEnergyCharacteristic.Notify
        service.characteristic.side_effect = [rx, tx]
        self.controller.discoveryFinished.connect.call_args.args[0]()
        service.stateChanged.connect.call_args.args[0](QLowEnergyService.RemoteServiceDiscovered)
        self.assertFalse(self.ble.ready)
        service.writeDescriptor.assert_called_once_with(tx.descriptor.return_value, b"\x01\x00")
        service.descriptorWritten.connect.call_args.args[0](tx.descriptor.return_value, b"\x01\x00")
        return service

    def test_complete_pairing_and_gatt_setup_waits_for_subscription_and_keeps_secrets_out_of_diagnostics(self):
        states = []
        self.ble.state_signal.connect(lambda state, detail: states.append(state))
        self.subscribed()
        self.assertTrue(self.ble.ready)
        self.assertEqual(states, [True])
        self.assertFalse(self.ble.connect_timer.isActive())
        self.ble.send("SET_WIFI,1,STA,MyPrivateNetwork,secret-password")
        recorded = " ".join(call.args[0] for call in self.ble.diagnostics.record.call_args_list)
        self.assertNotIn("secret-password", recorded)
        self.assertNotIn("MyPrivateNetwork", recorded)

    def test_gatt_failure_includes_phase_and_native_error_and_disconnects(self):
        service = self.subscribed()
        errors = []
        self.ble.error_signal.connect(errors.append)
        service.errorOccurred.connect.call_args.args[0](QLowEnergyService.CharacteristicWriteError)
        self.assertEqual(len(errors), 1)
        self.assertIn("CharacteristicWriteError", errors[0])
        self.assertIn("step: ready", errors[0])
        self.assertFalse(self.ble.ready)
        self.controller.disconnectFromDevice.assert_called_once()

    def test_pairing_timeout_names_the_step_and_releases_adapter(self):
        errors = []
        self.ble.error_signal.connect(errors.append)
        self.ble.connect("reader")
        self.ble._connection_timeout()
        self.assertIn("step: pairing", errors[0])
        self.assertIn("Windows", errors[0])
        self.assertIsNone(self.ble.local)
        self.central.assert_not_called()

    def test_cancelled_pairing_callbacks_cannot_affect_replacement(self):
        self.ble.connect("reader")
        old_paired = self.local.pairingFinished.connect.call_args.args[0]
        old_error = self.local.errorOccurred.connect.call_args.args[0]
        self.ble.disconnect()
        self.ble.connect("reader")
        generation = self.ble.generation
        old_paired(self.info.address(), QBluetoothLocalDevice.Paired)
        old_error(QBluetoothLocalDevice.PairingError)
        self.central.assert_not_called()
        self.assertEqual(self.ble.generation, generation)
        self.assertTrue(self.ble.connect_timer.isActive())

    def test_cancel_inside_controller_construction_releases_returned_controller(self):
        def cancelled(info, parent):
            self.ble.disconnect()
            return self.controller

        self.central.side_effect = cancelled
        self.local.pairingStatus.return_value = QBluetoothLocalDevice.Paired
        self.ble.connect("reader")
        self.controller.connectToDevice.assert_not_called()
        self.controller.disconnectFromDevice.assert_called_once()
        self.controller.deleteLater.assert_called_once()
        self.assertIsNone(self.ble.controller)

    def test_error_callback_starting_replacement_does_not_disconnect_it(self):
        self.ble.connect("reader")
        self.ble.error_signal.connect(lambda message: self.ble.connect("reader"))
        self.ble._fail("simulated error")
        self.assertIs(self.ble.local, self.local)
        self.assertTrue(self.ble.connect_timer.isActive())


if __name__ == "__main__":
    unittest.main(verbosity=2)
