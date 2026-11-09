package com.b3organ.physical;

import android.content.Context;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.os.Handler;
import android.os.Looper;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** Connects every available MIDI source to the organ. */
public final class MidiRouter implements AutoCloseable {
  public interface Listener {
    void midiNoteOn(int note, int velocity);
    void midiNoteOff(int note);
    void midiControl(int controller, int value);
  }

  private final MidiManager manager;
  private final Listener listener;
  private final Handler handler = new Handler(Looper.getMainLooper());
  private final List<MidiDevice> devices = new ArrayList<>();
  private final List<MidiOutputPort> ports = new ArrayList<>();
  private boolean opened = false;

  private final MidiReceiver receiver = new MidiReceiver() {
    @Override
    public void onSend(byte[] data, int offset, int count, long timestamp) {
      parse(data, offset, count);
    }
  };

  private final MidiManager.DeviceCallback deviceCallback =
      new MidiManager.DeviceCallback() {
        @Override
        public void onDeviceAdded(MidiDeviceInfo device) {
          openDevice(device);
        }

        @Override
        public void onDeviceRemoved(MidiDeviceInfo device) {
          closeAllPorts();
        }
      };

  public MidiRouter(Context context, Listener listener) {
    this.listener = listener;
    this.manager = (MidiManager) context.getSystemService(Context.MIDI_SERVICE);
  }

  public void open() {
    if (manager == null || opened) return;
    opened = true;
    manager.registerDeviceCallback(deviceCallback, handler);
    for (MidiDeviceInfo info : manager.getDevices()) openDevice(info);
  }

  private void openDevice(MidiDeviceInfo info) {
    if (manager == null) return;
    try {
      manager.openDevice(info, device -> {
        if (device == null) return;
        devices.add(device);
        for (int i = 0; i < info.getOutputPortCount(); i++) {
          MidiOutputPort port = device.openOutputPort(i);
          if (port != null) {
            port.connect(receiver);
            ports.add(port);
          }
        }
        // A device with no output ports is not a source; release it.
        if (info.getOutputPortCount() == 0) {
          try {
            device.close();
          } catch (IOException ignored) {
          }
          devices.remove(device);
        }
      }, handler);
    } catch (Exception ignored) {
    }
  }

  private void parse(byte[] data, int offset, int count) {
    int status = 0;
    int i = offset;
    final int end = offset + count;
    while (i < end) {
      int b = data[i] & 0xff;
      if (b >= 0xf8) {  // real-time messages can appear anywhere
        i++;
        continue;
      }
      if (b >= 0x80) {
        status = b;
        i++;
        if (status >= 0xf0) {
          // System common; skip data bytes conservatively.
          continue;
        }
      }
      if (status == 0) {
        i++;
        continue;
      }
      final int type = status & 0xf0;
      final int needed = (type == 0xc0 || type == 0xd0) ? 1 : 2;
      if (i + needed > end) break;
      final int d1 = data[i] & 0x7f;
      final int d2 = needed > 1 ? (data[i + 1] & 0x7f) : 0;
      i += needed;

      if (type == 0x90 && d2 > 0) {
        listener.midiNoteOn(d1, d2);
      } else if (type == 0x80 || (type == 0x90 && d2 == 0)) {
        listener.midiNoteOff(d1);
      } else if (type == 0xb0) {
        listener.midiControl(d1, d2);
      }
    }
  }

  private void closeAllPorts() {
    for (MidiOutputPort port : ports) {
      try {
        port.close();
      } catch (IOException ignored) {
      }
    }
    ports.clear();
    for (MidiDevice device : devices) {
      try {
        device.close();
      } catch (IOException ignored) {
      }
    }
    devices.clear();
  }

  @Override
  public void close() {
    if (manager != null && opened) {
      manager.unregisterDeviceCallback(deviceCallback);
      opened = false;
    }
    closeAllPorts();
  }
}
