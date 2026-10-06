package io.github.irontom10.caltransfer;

import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.SystemClock;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public final class SlcanUsbTransport {
    private final UsbDevice device;
    private final UsbDeviceConnection connection;
    private final UsbEndpoint input;
    private final UsbEndpoint output;
    private final List<UsbInterface> claimed;
    private final int controlInterfaceId;
    private final StringBuilder receiveText = new StringBuilder();
    private boolean canOpen;

    private SlcanUsbTransport(
            UsbDevice device,
            UsbDeviceConnection connection,
            UsbEndpoint input,
            UsbEndpoint output,
            List<UsbInterface> claimed,
            int controlInterfaceId) {
        this.device = device;
        this.connection = connection;
        this.input = input;
        this.output = output;
        this.claimed = claimed;
        this.controlInterfaceId = controlInterfaceId;
    }

    public static boolean isCandidate(UsbDevice device) {
        return findBulkEndpoint(device, UsbConstants.USB_DIR_IN) != null
                && findBulkEndpoint(device, UsbConstants.USB_DIR_OUT) != null;
    }

    public static SlcanUsbTransport create(UsbManager manager, UsbDevice device) {
        UsbDeviceConnection connection;
        UsbEndpoint input;
        UsbEndpoint output;
        List<UsbInterface> claimed;
        int controlInterfaceId;
        int i;

        if (manager == null || device == null || !isCandidate(device))
            return null;

        connection = manager.openDevice(device);
        if (connection == null)
            return null;

        input = findBulkEndpoint(device, UsbConstants.USB_DIR_IN);
        output = findBulkEndpoint(device, UsbConstants.USB_DIR_OUT);
        claimed = new ArrayList<>();
        controlInterfaceId = -1;

        for (i = 0; i < device.getInterfaceCount(); ++i) {
            UsbInterface intf = device.getInterface(i);
            if (intf.getInterfaceClass() == UsbConstants.USB_CLASS_COMM
                    && controlInterfaceId < 0) {
                controlInterfaceId = intf.getId();
            }

            if (hasEndpoint(intf, input) || hasEndpoint(intf, output)
                    || intf.getInterfaceClass() == UsbConstants.USB_CLASS_COMM) {
                if (connection.claimInterface(intf, true))
                    claimed.add(intf);
            }
        }

        if (input == null || output == null) {
            connection.close();
            return null;
        }

        if (controlInterfaceId < 0)
            controlInterfaceId = input.getAddress();

        return new SlcanUsbTransport(
                device, connection, input, output, claimed, controlInterfaceId);
    }

    public String label() {
        String product = device.getProductName();
        if (product == null || product.length() == 0)
            product = String.format(Locale.US, "%04X:%04X",
                    device.getVendorId(), device.getProductId());
        return product;
    }

    public synchronized boolean open(int bitrate) {
        String speed;

        speed = speedCommand(bitrate);
        if (speed == null)
            return false;

        configureCdc();
        receiveText.setLength(0);

        sendAscii("C\r");
        SystemClock.sleep(20);
        if (!sendAscii(speed + "\r"))
            return false;
        SystemClock.sleep(20);
        if (!sendAscii("O\r"))
            return false;

        canOpen = true;
        return true;
    }

    public synchronized void close() {
        if (canOpen) {
            sendAscii("C\r");
            canOpen = false;
        }
        receiveText.setLength(0);
    }

    public synchronized void shutdown() {
        int i;

        close();
        for (i = 0; i < claimed.size(); ++i)
            connection.releaseInterface(claimed.get(i));
        connection.close();
    }

    public synchronized boolean sendFrame(int canId, byte[] data) {
        StringBuilder line;
        int i;

        if (!canOpen || data == null || data.length > 8)
            return false;

        line = new StringBuilder(28);
        line.append('T');
        line.append(String.format(Locale.US, "%08X", canId & 0x1fffffff));
        line.append(Integer.toHexString(data.length).toUpperCase(Locale.US));
        for (i = 0; i < data.length; ++i)
            line.append(String.format(Locale.US, "%02X", data[i] & 0xff));
        line.append('\r');
        return sendAscii(line.toString());
    }

    /*
     * Return: four-byte big-endian CAN ID, one-byte DLC, then data bytes.
     * Native code ignores non-extended SLCAN records and adapter status lines.
     */
    public synchronized byte[] receiveFrame(int timeoutMs) {
        long deadline;
        byte[] buffer;

        if (!canOpen)
            return null;

        deadline = SystemClock.elapsedRealtime() + Math.max(timeoutMs, 0);
        buffer = new byte[Math.max(64, input.getMaxPacketSize())];

        for (;;) {
            byte[] parsed = takeFrame();
            if (parsed != null)
                return parsed;

            long remaining = deadline - SystemClock.elapsedRealtime();
            if (remaining <= 0)
                return null;

            int count = connection.bulkTransfer(
                    input, buffer, buffer.length, (int)Math.min(remaining, 250L));
            if (count < 0)
                continue;
            if (count == 0)
                continue;

            receiveText.append(new String(
                    buffer, 0, count, StandardCharsets.US_ASCII));
        }
    }

    private byte[] takeFrame() {
        int end;

        for (;;) {
            end = receiveText.indexOf("\r");
            if (end < 0)
                return null;

            String line = receiveText.substring(0, end);
            receiveText.delete(0, end + 1);

            if (line.length() < 10 || line.charAt(0) != 'T')
                continue;

            try {
                long idLong = Long.parseLong(line.substring(1, 9), 16);
                int dlc = Integer.parseInt(line.substring(9, 10), 16);
                int required = 10 + dlc * 2;
                int i;

                if (dlc < 0 || dlc > 8 || line.length() < required)
                    continue;

                byte[] frame = new byte[5 + dlc];
                int id = (int)(idLong & 0x1fffffffL);
                frame[0] = (byte)(id >>> 24);
                frame[1] = (byte)(id >>> 16);
                frame[2] = (byte)(id >>> 8);
                frame[3] = (byte)id;
                frame[4] = (byte)dlc;

                for (i = 0; i < dlc; ++i)
                    frame[5 + i] = (byte)Integer.parseInt(
                            line.substring(10 + i * 2, 12 + i * 2), 16);

                return frame;
            }
            catch (RuntimeException ignored) {
            }
        }
    }

    private boolean sendAscii(String text) {
        byte[] bytes = text.getBytes(StandardCharsets.US_ASCII);
        int written = connection.bulkTransfer(output, bytes, bytes.length, 1000);
        return written == bytes.length;
    }

    private void configureCdc() {
        if (controlInterfaceId < 0)
            return;

        byte[] lineCoding = new byte[] {
                0x00, (byte)0xC2, 0x01, 0x00, 0x00, 0x00, 0x08
        };

        connection.controlTransfer(
                0x21, 0x20, 0, controlInterfaceId,
                lineCoding, lineCoding.length, 1000);
        connection.controlTransfer(
                0x21, 0x22, 0x03, controlInterfaceId,
                null, 0, 1000);
    }

    private static String speedCommand(int bitrate) {
        switch (bitrate) {
        case 125000:
            return "S4";
        case 250000:
            return "S5";
        case 500000:
            return "S6";
        case 1000000:
            return "S8";
        default:
            return null;
        }
    }

    private static UsbEndpoint findBulkEndpoint(UsbDevice device, int direction) {
        int i;
        int j;

        for (i = 0; i < device.getInterfaceCount(); ++i) {
            UsbInterface intf = device.getInterface(i);
            for (j = 0; j < intf.getEndpointCount(); ++j) {
                UsbEndpoint ep = intf.getEndpoint(j);
                if (ep.getType() == UsbConstants.USB_ENDPOINT_XFER_BULK
                        && ep.getDirection() == direction) {
                    return ep;
                }
            }
        }
        return null;
    }

    private static boolean hasEndpoint(UsbInterface intf, UsbEndpoint endpoint) {
        int i;

        if (endpoint == null)
            return false;

        for (i = 0; i < intf.getEndpointCount(); ++i) {
            if (intf.getEndpoint(i).getAddress() == endpoint.getAddress())
                return true;
        }
        return false;
    }
}
