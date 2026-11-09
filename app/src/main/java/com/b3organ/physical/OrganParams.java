package com.b3organ.physical;

import android.content.SharedPreferences;

/** Mutable organ state shared by the UI, MIDI input and the native engine. */
public final class OrganParams {
  public static final int DRAWBARS = 9;

  public static final String[] PRESET_NAMES = {
      "JAZZ", "BLUES", "GOSPEL", "ROCK", "THEATRE", "FLUTE"
  };

  private static final float[][] PRESET_BARS = {
      {8, 8, 8, 0, 0, 0, 0, 0, 0},
      {8, 8, 8, 4, 0, 0, 0, 0, 0},
      {8, 8, 8, 8, 8, 8, 0, 0, 0},
      {8, 8, 8, 8, 8, 8, 8, 8, 8},
      {8, 0, 8, 0, 8, 0, 8, 0, 0},
      {0, 0, 8, 0, 0, 0, 0, 0, 0},
  };

  public final float[] drawbars = {8, 0, 0, 0, 0, 0, 0, 0, 0};

  public boolean percussionOn = false;
  public boolean percussionSoft = false;
  public boolean percussionSlow = false;
  public boolean percussionThird = true;

  /** 0 = off, 1..3 = V1..V3, 4..6 = C1..C3. */
  public int scanner = 4;
  /** 0 = brake, 1 = slow (chorale), 2 = fast (tremolo). */
  public int leslieSpeed = 1;

  public float drive = 0.28f;
  public float keyClick = 0.55f;
  public float leakage = 0.30f;
  public float hum = 0.20f;
  public float reverb = 0.22f;
  public float volume = 0.72f;
  public float expression = 1.0f;
  public float micDistance = 0.45f;
  public float micSpread = 0.50f;
  public float hornDrum = 0.50f;
  public float tone = 0.50f;
  public float scannerRate = 6.0f;
  public float wear = 0.35f;
  public float cabinet = 1.0f;

  public void applyPreset(int index) {
    if (index < 0 || index >= PRESET_NAMES.length) return;
    System.arraycopy(PRESET_BARS[index], 0, drawbars, 0, DRAWBARS);
    switch (index) {
      case 0:  // JAZZ
        percussionOn = false;
        percussionThird = true;
        scanner = 4;
        leslieSpeed = 1;
        drive = 0.24f;
        keyClick = 0.55f;
        reverb = 0.22f;
        micDistance = 0.45f;
        break;
      case 1:  // BLUES
        percussionOn = true;
        percussionSoft = false;
        percussionSlow = false;
        percussionThird = true;
        scanner = 5;
        leslieSpeed = 1;
        drive = 0.36f;
        keyClick = 0.68f;
        reverb = 0.20f;
        break;
      case 2:  // GOSPEL
        percussionOn = true;
        percussionSoft = false;
        percussionSlow = false;
        percussionThird = true;
        scanner = 6;
        leslieSpeed = 2;
        drive = 0.40f;
        keyClick = 0.75f;
        reverb = 0.30f;
        break;
      case 3:  // ROCK
        percussionOn = false;
        scanner = 2;
        leslieSpeed = 2;
        drive = 0.58f;
        keyClick = 0.80f;
        reverb = 0.25f;
        break;
      case 4:  // THEATRE
        percussionOn = true;
        percussionSoft = true;
        percussionSlow = true;
        percussionThird = false;
        scanner = 1;
        leslieSpeed = 0;
        drive = 0.18f;
        keyClick = 0.45f;
        reverb = 0.48f;
        micDistance = 0.65f;
        break;
      default:  // FLUTE
        percussionOn = false;
        scanner = 2;
        leslieSpeed = 1;
        drive = 0.12f;
        keyClick = 0.30f;
        reverb = 0.35f;
        break;
    }
  }

  public float[] toFloats() {
    float[] f = new float[23];
    System.arraycopy(drawbars, 0, f, 0, DRAWBARS);
    f[9] = drive;
    f[10] = keyClick;
    f[11] = leakage;
    f[12] = hum;
    f[13] = reverb;
    f[14] = volume;
    f[15] = expression;
    f[16] = micDistance;
    f[17] = micSpread;
    f[18] = hornDrum;
    f[19] = tone;
    f[20] = scannerRate;
    f[21] = wear;
    f[22] = cabinet;
    return f;
  }

  public int[] toInts() {
    return new int[]{
        percussionOn ? 1 : 0,
        percussionSoft ? 1 : 0,
        percussionSlow ? 1 : 0,
        percussionThird ? 1 : 0,
        scanner,
        leslieSpeed
    };
  }

  public void save(SharedPreferences.Editor e) {
    for (int i = 0; i < DRAWBARS; i++) e.putFloat("db" + i, drawbars[i]);
    e.putBoolean("pOn", percussionOn);
    e.putBoolean("pSoft", percussionSoft);
    e.putBoolean("pSlow", percussionSlow);
    e.putBoolean("pThird", percussionThird);
    e.putInt("scanner", scanner);
    e.putInt("speed", leslieSpeed);
    e.putFloat("drive", drive);
    e.putFloat("click", keyClick);
    e.putFloat("leakage", leakage);
    e.putFloat("hum", hum);
    e.putFloat("reverb", reverb);
    e.putFloat("volume", volume);
    e.putFloat("micDistance", micDistance);
    e.putFloat("micSpread", micSpread);
    e.putFloat("hornDrum", hornDrum);
    e.putFloat("tone", tone);
    e.putFloat("wear", wear);
  }

  public void load(SharedPreferences sp) {
    for (int i = 0; i < DRAWBARS; i++) drawbars[i] = sp.getFloat("db" + i, drawbars[i]);
    percussionOn = sp.getBoolean("pOn", percussionOn);
    percussionSoft = sp.getBoolean("pSoft", percussionSoft);
    percussionSlow = sp.getBoolean("pSlow", percussionSlow);
    percussionThird = sp.getBoolean("pThird", percussionThird);
    scanner = sp.getInt("scanner", scanner);
    leslieSpeed = sp.getInt("speed", leslieSpeed);
    drive = sp.getFloat("drive", drive);
    keyClick = sp.getFloat("click", keyClick);
    leakage = sp.getFloat("leakage", leakage);
    hum = sp.getFloat("hum", hum);
    reverb = sp.getFloat("reverb", reverb);
    volume = sp.getFloat("volume", volume);
    micDistance = sp.getFloat("micDistance", micDistance);
    micSpread = sp.getFloat("micSpread", micSpread);
    hornDrum = sp.getFloat("hornDrum", hornDrum);
    tone = sp.getFloat("tone", tone);
    wear = sp.getFloat("wear", wear);
  }
}
