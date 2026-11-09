package com.b3organ.physical;

/** Thin JNI facade over the native engine and its Oboe audio stream. */
public final class B3Engine implements AutoCloseable {
  private long handle;

  public B3Engine() {
    handle = nativeCreate(48000);
  }

  public boolean isValid() {
    return handle != 0;
  }

  public void start() {
    if (handle != 0) nativeStart(handle);
  }

  public void stop() {
    if (handle != 0) nativeStop(handle);
  }

  public void noteOn(int midiNote, float velocity) {
    if (handle != 0) nativeNoteOn(handle, midiNote, velocity);
  }

  public void noteOff(int midiNote) {
    if (handle != 0) nativeNoteOff(handle, midiNote);
  }

  public void allNotesOff() {
    if (handle != 0) nativeAllNotesOff(handle);
  }

  public void setParams(OrganParams p) {
    if (handle != 0) nativeSetParams(handle, p.toFloats(), p.toInts());
  }

  /** out: [hornDeg, drumDeg, scannerDeg, peak, activeNotes, reserved]. */
  public void telemetry(float[] out) {
    if (handle != 0) nativeGetTelemetry(handle, out);
  }

  @Override
  public void close() {
    if (handle != 0) {
      nativeDestroy(handle);
      handle = 0;
    }
  }

  private static native long nativeCreate(int sampleRate);
  private static native void nativeDestroy(long handle);
  private static native void nativeStart(long handle);
  private static native void nativeStop(long handle);
  private static native void nativeNoteOn(long handle, int note, float velocity);
  private static native void nativeNoteOff(long handle, int note);
  private static native void nativeAllNotesOff(long handle);
  private static native void nativeSetParams(long handle, float[] floats, int[] ints);
  private static native void nativeGetTelemetry(long handle, float[] out);

  static {
    System.loadLibrary("b3jni");
  }
}
