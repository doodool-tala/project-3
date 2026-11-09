package com.b3organ.physical;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;

/** Full-screen B-3 panel. */
public final class MainActivity extends Activity
    implements OrganView.Host, MidiRouter.Listener {

  private B3Engine engine;
  private OrganParams params;
  private OrganView view;
  private MidiRouter midi;
  private AudioManager audioManager;
  private AudioFocusRequest focusRequest;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    hideSystemUi();

    params = new OrganParams();
    final SharedPreferences prefs = getSharedPreferences("b3organ", MODE_PRIVATE);
    params.load(prefs);

    engine = new B3Engine();
    engine.setParams(params);

    view = new OrganView(this, params);
    view.setHost(this);
    setContentView(view);

    audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
    focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN).build();
    midi = new MidiRouter(this, this);
  }

  private void hideSystemUi() {
    View decor = getWindow().getDecorView();
    decor.setSystemUiVisibility(
        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
  }

  @Override
  public void onWindowFocusChanged(boolean hasFocus) {
    super.onWindowFocusChanged(hasFocus);
    if (hasFocus) hideSystemUi();
  }

  @Override
  protected void onResume() {
    super.onResume();
    if (audioManager != null) {
      audioManager.requestAudioFocus(focusRequest);
    }
    engine.start();
    engine.setParams(params);
    view.setAnimating(true);
    midi.open();
  }

  @Override
  protected void onPause() {
    view.setAnimating(false);
    engine.allNotesOff();
    engine.stop();
    midi.close();
    if (audioManager != null) audioManager.abandonAudioFocusRequest(focusRequest);
    SharedPreferences.Editor editor = getSharedPreferences("b3organ", MODE_PRIVATE).edit().clear();
    params.save(editor);
    editor.apply();
    super.onPause();
  }

  @Override
  protected void onDestroy() {
    engine.close();
    super.onDestroy();
  }

  // ---- OrganView.Host ----

  @Override
  public void paramsChanged(OrganParams p) {
    engine.setParams(p);
  }

  @Override
  public void noteOn(int midiNote) {
    engine.noteOn(midiNote, 1.0f);
  }

  @Override
  public void noteOff(int midiNote) {
    engine.noteOff(midiNote);
  }

  @Override
  public void getTelemetry(float[] out) {
    engine.telemetry(out);
  }

  // ---- MidiRouter.Listener ----

  @Override
  public void midiNoteOn(int note, int velocity) {
    engine.noteOn(note, 0.35f + 0.65f * (velocity / 127.0f));
  }

  @Override
  public void midiNoteOff(int note) {
    engine.noteOff(note);
  }

  @Override
  public void midiControl(int controller, int value) {
    final float v = value / 127.0f;
    runOnUiThread(() -> {
      if (controller == 11) {  // expression pedal
        params.expression = v;
      } else if (controller == 7) {  // channel volume
        params.volume = v;
      } else if (controller == 1) {  // mod wheel: rotor speed
        params.leslieSpeed = value >= 64 ? 2 : 1;
      } else {
        return;
      }
      engine.setParams(params);
      view.invalidate();
    });
  }
}
