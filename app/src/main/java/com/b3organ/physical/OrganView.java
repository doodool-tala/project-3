package com.b3organ.physical;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.Build;
import android.os.SystemClock;
import android.util.Log;
import android.util.SparseIntArray;
import android.view.MotionEvent;
import android.view.View;

import java.util.Random;

/**
 * Hand-drawn B-3 panel. Everything is laid out on a fixed 1800 x 1000 virtual
 * canvas and scaled to the device, so the panel keeps its proportions in
 * landscape on phones and tablets.
 */
public final class OrganView extends View {

  public interface Host {
    void paramsChanged(OrganParams p);

    void noteOn(int midiNote);

    void noteOff(int midiNote);

    /** out: [hornDeg, drumDeg, scannerDeg, peak, activeNotes, reserved]. */
    void getTelemetry(float[] out);
  }

  private static final float VW = 1800f;
  private static final float VH = 1000f;

  private static final int LOW_NOTE = 36;   // C2
  private static final int HIGH_NOTE = 96;  // C7
  private static final int WHITE_KEYS = 36;
  private static final int BLACK_KEYS = 25;

  private static final int COL_BG = 0xFF0C0704;
  private static final int COL_GOLD = 0xFFD8B45A;
  private static final int COL_CREAM = 0xFFF2E3BC;
  private static final int COL_DIM = 0xFF9B8E78;
  private static final int COL_PANEL = 0xFF1B1714;
  private static final int COL_LINE = 0xFF5A4A34;
  private static final int COL_LED = 0xFFFF5A36;

  private static final int CTRL_NONE = -1;

  private final OrganParams params;
  private Host host;
  private boolean animating = false;

  private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final RectF tmp = new RectF();

  private final Typeface serifBold = Typeface.create(Typeface.SERIF, Typeface.BOLD);
  private final Typeface sans = Typeface.create("sans-serif", Typeface.NORMAL);
  private final Typeface sansBold = Typeface.create("sans-serif", Typeface.BOLD);
  private final Typeface mono = Typeface.create("monospace", Typeface.BOLD);

  private float scale = 1f;
  private float offX = 0f;
  private float offY = 0f;
  private Bitmap wood;
  private final LinearGradient panelGradient =
      new LinearGradient(0, 100, 0, 620,
          new int[]{0xFF2A2522, 0xFF1D1917, 0xFF131110, 0xFF1A1614},
          null, Shader.TileMode.CLAMP);
  private final LinearGradient stripGradient =
      new LinearGradient(0, 632, 0, 792,
          new int[]{0xFF26211D, 0xFF17130F}, null, Shader.TileMode.CLAMP);
  private final LinearGradient whiteKeyGradient =
      new LinearGradient(0, 818, 0, 974,
          new int[]{0xFFFBF2D9, 0xFFF0E2BF, 0xFFD7C69E},
          null, Shader.TileMode.CLAMP);
  private final LinearGradient blackKeyGradient =
      new LinearGradient(0, 818, 0, 928,
          new int[]{0xFF3A3733, 0xFF141210, 0xFF070606},
          null, Shader.TileMode.CLAMP);

  // Geometry ----------------------------------------------------------------
  private final RectF panel = new RectF(24, 100, 1776, 620);
  private final RectF knobPanel = new RectF(24, 632, 1776, 792);
  private final float kbLeft = 34f;
  private final float kbRight = 1766f;
  private final float kbTop = 818f;
  private final float kbBottom = 974f;
  private float whiteW = (kbRight - kbLeft) / WHITE_KEYS;
  private float blackW = 0f;
  private final float[] blackLeft = new float[BLACK_KEYS];
  private final int[] blackNote = new int[BLACK_KEYS];
  private final int[] whiteNote = new int[WHITE_KEYS];
  private final float[] whiteLeft = new float[WHITE_KEYS];

  private final RectF[] presetRects = new RectF[OrganParams.PRESET_NAMES.length];
  private final RectF[] percRects = new RectF[4];
  private final RectF[] scannerRects = new RectF[7];
  private final RectF[] speedRects = new RectF[3];
  private static final int[] SPEED_VALUES = {0, 1, 2};
  private static final int[] SCANNER_VALUES = {0, 1, 2, 3, 4, 5, 6};
  private static final String[] SCANNER_LABELS = {"OFF", "V1", "V2", "V3", "C1", "C2", "C3"};
  private static final String[] KNOB_LABELS = {"DRIVE", "CLICK", "REVERB", "MIC DIST", "WEAR", "VOLUME"};
  private final float[] knobX = new float[6];
  private final float knobY = 704f;
  private final float knobR = 45f;
  private final RectF drawbarHit = new RectF(60, 150, 900, 610);

  private final float[] telemetry = new float[6];
  private final SparseIntArray pointerNotes = new SparseIntArray();
  private final boolean[] held = new boolean[HIGH_NOTE - LOW_NOTE + 1];

  private int dragDrawbar = CTRL_NONE;
  private int dragKnob = CTRL_NONE;
  private float dragStartY = 0f;
  private float dragStartValue = 0f;
  private int dragPointer = -1;

  private long lastFrameMs = 0;
  private float displayPeak = 0f;
  private boolean loggedFirstDraw = false;

  public OrganView(Context context, OrganParams params) {
    super(context);
    this.params = params;
    buildLayout();
    setFocusable(true);
    setKeepScreenOn(true);
  }

  public void setHost(Host host) {
    this.host = host;
  }

  public void setAnimating(boolean animating) {
    this.animating = animating;
    if (animating) {
      lastFrameMs = SystemClock.uptimeMillis();
      postInvalidateOnAnimation();
    }
  }

  private void buildLayout() {
    blackW = whiteW * 0.62f;
    int wi = 0;
    int bi = 0;
    for (int n = LOW_NOTE; n <= HIGH_NOTE; n++) {
      if (isBlack(n)) {
        blackNote[bi] = n;
        blackLeft[bi] = kbLeft + wi * whiteW - blackW * 0.5f;
        bi++;
      } else {
        whiteNote[wi] = n;
        whiteLeft[wi] = kbLeft + wi * whiteW;
        wi++;
      }
    }
    for (int i = 0; i < presetRects.length; i++) {
      presetRects[i] = new RectF(596 + i * 196, 20, 596 + i * 196 + 188, 76);
    }
    percRects[0] = new RectF(942, 176, 1072, 266);
    percRects[1] = new RectF(1082, 176, 1202, 266);
    percRects[2] = new RectF(942, 276, 1072, 366);
    percRects[3] = new RectF(1082, 276, 1202, 366);
    for (int i = 0; i < 7; i++) {
      float x = 1236 + (i / 4) * 116;
      float y = 332 + (i % 4) * 56;
      scannerRects[i] = new RectF(x, y, x + 108, y + 46);
    }
    speedRects[0] = new RectF(1505, 176, 1612, 224);
    speedRects[1] = new RectF(1505, 236, 1612, 284);
    speedRects[2] = new RectF(1505, 296, 1612, 344);
    for (int i = 0; i < 6; i++) knobX[i] = 170 + i * 292;
    // The keyboard must be wide enough for 36 white keys.
    whiteW = (kbRight - kbLeft) / WHITE_KEYS;
    blackW = whiteW * 0.62f;
  }

  private static boolean isBlack(int midi) {
    switch (midi % 12) {
      case 1: case 3: case 6: case 8: case 10:
        return true;
      default:
        return false;
    }
  }

  @Override
  protected void onSizeChanged(int w, int h, int oldw, int oldh) {
    super.onSizeChanged(w, h, oldw, oldh);
    scale = Math.min(w / VW, h / VH);
    if (scale <= 0f) scale = 1f;
    offX = (w - VW * scale) * 0.5f;
    offY = (h - VH * scale) * 0.5f;
    if (wood == null) wood = makeWood();
    Log.i("B3Ui", "view size " + w + "x" + h + " scale " + scale + " offset " + offX + "," + offY);
  }

  private Bitmap makeWood() {
    final int w = 900;
    final int h = 500;
    Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
    Canvas c = new Canvas(bmp);
    Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    p.setShader(new LinearGradient(0, 0, w, h,
        new int[]{0xFF3A2113, 0xFF55301A, 0xFF31190C, 0xFF5F3A1E, 0xFF2A150A},
        null, Shader.TileMode.CLAMP));
    c.drawRect(0, 0, w, h, p);

    Random r = new Random(19550428L);
    for (int i = 0; i < 240; i++) {
      float x = r.nextFloat() * w;
      Paint g = new Paint(Paint.ANTI_ALIAS_FLAG);
      g.setColor(r.nextBoolean() ? 0x16000000 : 0x10FFDCA0);
      g.setStrokeWidth(0.6f + r.nextFloat() * 2.0f);
      Path path = new Path();
      path.moveTo(x, 0);
      path.cubicTo(x + r.nextInt(26) - 13, h * 0.33f,
          x + r.nextInt(34) - 17, h * 0.66f,
          x + r.nextInt(24) - 12, h);
      c.drawPath(path, g);
    }
    // A couple of darker knots in the veneer.
    for (int i = 0; i < 5; i++) {
      float x = r.nextFloat() * w;
      float y = r.nextFloat() * h;
      Paint k = new Paint(Paint.ANTI_ALIAS_FLAG);
      k.setColor(0x22000000);
      k.setStyle(Paint.Style.STROKE);
      k.setStrokeWidth(1.2f + r.nextFloat());
      c.drawOval(x - 18, y - 7, x + 18, y + 7, k);
      c.drawOval(x - 10, y - 3, x + 10, y + 3, k);
    }
    Paint varnish = new Paint();
    varnish.setShader(new LinearGradient(0, 0, 0, h,
        new int[]{0x38000000, 0x00FFFFFF, 0x22000000}, new float[]{0f, 0.45f, 1f},
        Shader.TileMode.CLAMP));
    c.drawRect(0, 0, w, h, varnish);
    return bmp;
  }

  @Override
  protected void onDraw(Canvas canvas) {
    super.onDraw(canvas);
    if (host != null) host.getTelemetry(telemetry);
    final long now = SystemClock.uptimeMillis();
    if (lastFrameMs == 0) lastFrameMs = now;
    lastFrameMs = now;
    displayPeak = Math.max(telemetry[3], displayPeak * 0.90f);

    canvas.drawColor(COL_BG);
    int save = canvas.save();
    canvas.translate(offX, offY);
    canvas.scale(scale, scale);

    if (wood != null) {
      canvas.drawBitmap(wood, null, new RectF(0, 0, VW, VH), fill);
    }
    drawHeader(canvas);
    drawPanel(canvas);
    drawDrawbars(canvas);
    drawPercussion(canvas);
    drawScanner(canvas);
    drawLeslie(canvas);
    drawKnobs(canvas);
    drawKeyboard(canvas);
    drawFooter(canvas);

    canvas.restoreToCount(save);
    if (!loggedFirstDraw) {
      loggedFirstDraw = true;
      Log.i("B3Ui", "panel drawn " + getWidth() + "x" + getHeight()
          + " scale " + scale + " peak " + telemetry[3]);
    }
    if (animating) postInvalidateOnAnimation();
  }

  private void drawHeader(Canvas c) {
    fill.setShader(null);
    fill.setColor(0x66000000);
    c.drawRect(0, 96, VW, 100, fill);

    text.setTypeface(serifBold);
    text.setTextSize(58);
    text.setTextAlign(Paint.Align.LEFT);
    text.setColor(0xFF120C06);
    c.drawText("B-3", 44, 72, text);
    text.setColor(COL_GOLD);
    c.drawText("B-3", 41, 68, text);

    text.setTypeface(sansBold);
    text.setTextSize(19);
    text.setLetterSpacing(0.16f);
    text.setColor(COL_CREAM);
    c.drawText("PHYSICAL TONEWHEEL MODEL", 178, 54, text);
    text.setLetterSpacing(0.08f);
    text.setTextSize(13);
    text.setColor(COL_DIM);
    c.drawText("91 WHEELS  ·  SCANNER  ·  ROTARY", 178, 78, text);
    text.setLetterSpacing(0f);

    for (int i = 0; i < presetRects.length; i++) {
      RectF r = presetRects[i];
      fill.setColor(0xFF171310);
      c.drawRoundRect(r, 8, 8, fill);
      stroke.setStyle(Paint.Style.STROKE);
      stroke.setStrokeWidth(1.2f);
      stroke.setColor(0xFF6A5436);
      c.drawRoundRect(r, 8, 8, stroke);
      stroke.setStyle(Paint.Style.FILL);
      text.setTypeface(sansBold);
      text.setTextSize(16);
      text.setTextAlign(Paint.Align.CENTER);
      text.setColor(COL_CREAM);
      c.drawText(OrganParams.PRESET_NAMES[i], r.centerX(), r.centerY() + 6, text);
    }
  }

  private void drawPanel(Canvas c) {
    fill.setShader(panelGradient);
    fill.setStyle(Paint.Style.FILL);
    c.drawRoundRect(panel, 16, 16, fill);
    fill.setShader(null);
    stroke.setStyle(Paint.Style.STROKE);
    stroke.setStrokeWidth(2.0f);
    stroke.setColor(0xFF6B5333);
    c.drawRoundRect(panel, 16, 16, stroke);
    stroke.setStrokeWidth(1f);
    stroke.setColor(0x55000000);
    c.drawRoundRect(new RectF(panel.left + 3, panel.top + 3, panel.right - 3, panel.bottom - 3),
        14, 14, stroke);
    stroke.setStyle(Paint.Style.FILL);

    // Section dividers.
    stroke.setColor(0x33D8B45A);
    stroke.setStrokeWidth(1.5f);
    c.drawLine(916, 128, 916, 596, stroke);
    c.drawLine(1214, 128, 1214, 596, stroke);
    c.drawLine(1478, 128, 1478, 596, stroke);
    stroke.setStyle(Paint.Style.FILL);

    // Screws.
    for (float x : new float[]{44, 1756}) {
      for (float y : new float[]{118, 602}) {
        fill.setColor(0xFF3A342C);
        c.drawCircle(x, y, 5, fill);
        fill.setColor(0xFF807868);
        c.drawCircle(x - 1, y - 1, 3, fill);
      }
    }

    // Control strip.
    fill.setShader(stripGradient);
    c.drawRoundRect(knobPanel, 14, 14, fill);
    fill.setShader(null);
    stroke.setColor(0xFF5A472E);
    stroke.setStrokeWidth(1.5f);
    stroke.setStyle(Paint.Style.STROKE);
    c.drawRoundRect(knobPanel, 14, 14, stroke);
    stroke.setStyle(Paint.Style.FILL);
    sectionTitle(c, "VOICING", 44, 656);
  }

  private void sectionTitle(Canvas c, String s, float x, float y) {
    text.setTypeface(sansBold);
    text.setTextSize(15);
    text.setTextAlign(Paint.Align.LEFT);
    text.setLetterSpacing(0.18f);
    text.setColor(COL_GOLD);
    c.drawText(s, x, y, text);
    text.setLetterSpacing(0f);
  }

  private void drawDrawbars(Canvas c) {
    sectionTitle(c, "HARMONIC DRAWBARS", 44, 142);
    final float left = 64f;
    final float right = 896f;
    final float colW = (right - left) / 9f;
    final float slotTop = 180f;
    final float slotBottom = 540f;
    final float travel = slotBottom - slotTop;

    for (int i = 0; i < 9; i++) {
      float cx = left + (i + 0.5f) * colW;

      text.setTypeface(sansBold);
      text.setTextSize(18);
      text.setTextAlign(Paint.Align.CENTER);
      text.setColor(COL_CREAM);
      c.drawText(labelForBar(i), cx, 170, text);

      // Slot.
      tmp.set(cx - 11, slotTop, cx + 11, slotBottom);
      fill.setColor(0xFF070605);
      c.drawRoundRect(tmp, 8, 8, fill);
      stroke.setColor(0x66000000);
      stroke.setStrokeWidth(2f);
      stroke.setStyle(Paint.Style.STROKE);
      c.drawRoundRect(tmp, 8, 8, stroke);
      stroke.setStyle(Paint.Style.FILL);

      float v = clamp(params.drawbars[i], 0f, 8f);
      float knobY = slotBottom - (v / 8f) * travel;
      // Chrome drawbar knob.
      tmp.set(cx - 38, knobY - 13, cx + 38, knobY + 13);
      fill.setColor(0xFF11100E);
      c.drawRoundRect(new RectF(tmp.left + 2, tmp.top + 4, tmp.right + 2, tmp.bottom + 4),
          7, 7, fill);
      fill.setColor(0xFFC9C2B2);
      c.drawRoundRect(tmp, 7, 7, fill);
      fill.setColor(0x88FFFFFF);
      c.drawRect(cx - 34, knobY - 11, cx + 34, knobY - 7, fill);
      fill.setColor(0x44000000);
      c.drawRect(cx - 34, knobY + 8, cx + 34, knobY + 11, fill);
      stroke.setColor(0xFF59503F);
      stroke.setStrokeWidth(1.4f);
      stroke.setStyle(Paint.Style.STROKE);
      c.drawRoundRect(tmp, 7, 7, stroke);
      stroke.setStyle(Paint.Style.FILL);
      fill.setColor(0x33000000);
      for (int g = -2; g <= 2; g++) {
        c.drawRect(cx + g * 12f - 0.7f, knobY - 9, cx + g * 12f + 0.7f, knobY + 9, fill);
      }

      text.setTypeface(mono);
      text.setTextSize(20);
      text.setColor(COL_GOLD);
      c.drawText(String.valueOf((int) v), cx, 578, text);
      text.setTypeface(sans);
      text.setTextSize(12);
      text.setColor(COL_DIM);
      c.drawText(String.valueOf(i + 1), cx, 600, text);
    }
  }

  private String labelForBar(int i) {
    switch (i) {
      case 0: return "16'";
      case 1: return "5⅓'";
      case 2: return "8'";
      case 3: return "4'";
      case 4: return "2⅔'";
      case 5: return "2'";
      case 6: return "1⅗'";
      case 7: return "1⅓'";
      default: return "1'";
    }
  }

  private void drawPercussion(Canvas c) {
    sectionTitle(c, "PERCUSSION", 942, 142);
    drawToggle(c, percRects[0], "PERC", params.percussionOn ? "ON" : "OFF", params.percussionOn);
    drawToggle(c, percRects[1], "LEVEL", params.percussionSoft ? "SOFT" : "NORMAL", !params.percussionSoft);
    drawToggle(c, percRects[2], "DECAY", params.percussionSlow ? "SLOW" : "FAST", !params.percussionSlow);
    drawToggle(c, percRects[3], "HARM", params.percussionThird ? "3rd" : "2nd", true);

    text.setTypeface(sans);
    text.setTextSize(12);
    text.setTextAlign(Paint.Align.LEFT);
    text.setColor(COL_DIM);
    c.drawText("Single trigger, drawn from the", 944, 404, text);
    c.drawText("manual busbar. Percussion is", 944, 422, text);
    c.drawText("routed around the scanner, as", 944, 440, text);
    c.drawText("on the original instrument.", 944, 458, text);

    text.setTypeface(mono);
    text.setTextSize(13);
    text.setColor(COL_GOLD);
    c.drawText(params.percussionThird ? "3rd harmonic" : "2nd harmonic", 944, 500, text);
    text.setTypeface(sans);
    text.setColor(COL_DIM);
    text.setTextSize(12);
    c.drawText(params.percussionSlow ? "slow decay ≈ 340 ms" : "fast decay ≈ 115 ms", 944, 522, text);
  }

  private void drawScanner(Canvas c) {
    sectionTitle(c, "SCANNER", 1238, 142);
    float cx = 1347f;
    float cy = 244f;
    float r = 54f;

    fill.setColor(0xFF0A0908);
    c.drawCircle(cx, cy + 3, r + 9, fill);
    fill.setColor(0xFFB6AE9E);
    c.drawCircle(cx, cy, r + 7, fill);
    fill.setColor(0xFF1D1A16);
    c.drawCircle(cx, cy, r, fill);
    fill.setColor(0xFF0E0C0A);
    c.drawCircle(cx, cy, r - 8, fill);

    int mode = Math.max(0, Math.min(6, params.scanner));
    float frac = mode / 6f;
    stroke.setColor(COL_GOLD);
    stroke.setStrokeWidth(3f);
    stroke.setStyle(Paint.Style.STROKE);
    tmp.set(cx - r - 4, cy - r - 4, cx + r + 4, cy + r + 4);
    c.drawArc(tmp, 135f, 270f * frac, false, stroke);
    stroke.setStyle(Paint.Style.FILL);

    double ang = Math.toRadians(135.0 + 270.0 * frac);
    fill.setColor(0xFFF3E5BE);
    c.drawCircle(cx + (float) Math.cos(ang) * (r - 20), cy + (float) Math.sin(ang) * (r - 20),
        5f, fill);
    stroke.setColor(0xFFE6D3A0);
    stroke.setStrokeWidth(3f);
    c.drawLine(cx, cy, cx + (float) Math.cos(ang) * (r - 24),
        cy + (float) Math.sin(ang) * (r - 24), stroke);

    text.setTypeface(mono);
    text.setTextSize(20);
    text.setTextAlign(Paint.Align.CENTER);
    text.setColor(COL_CREAM);
    c.drawText(SCANNER_LABELS[mode], cx, 320, text);

    for (int i = 0; i < 7; i++) {
      RectF rr = scannerRects[i];
      boolean on = params.scanner == SCANNER_VALUES[i];
      fill.setColor(on ? 0xFF3B2E1B : 0xFF151210);
      c.drawRoundRect(rr, 7, 7, fill);
      stroke.setColor(on ? COL_GOLD : 0xFF473D30);
      stroke.setStrokeWidth(on ? 2f : 1f);
      stroke.setStyle(Paint.Style.STROKE);
      c.drawRoundRect(rr, 7, 7, stroke);
      stroke.setStyle(Paint.Style.FILL);
      text.setTypeface(on ? sansBold : sans);
      text.setTextSize(15);
      text.setColor(on ? COL_CREAM : COL_DIM);
      c.drawText(SCANNER_LABELS[i], rr.centerX(), rr.centerY() + 5, text);
    }

    text.setTypeface(sans);
    text.setTextSize(11);
    text.setColor(COL_DIM);
    c.drawText("6 Hz · 8-pole", cx, 560, text);
    c.drawText("scanner line", cx, 576, text);
  }

  private void drawLeslie(Canvas c) {
    sectionTitle(c, "LESLIE", 1505, 142);
    String[] labels = {"BRAKE", "SLOW", "FAST"};
    for (int i = 0; i < 3; i++) {
      RectF rr = speedRects[i];
      boolean on = params.leslieSpeed == SPEED_VALUES[i];
      fill.setColor(on ? 0xFF3B2E1B : 0xFF151210);
      c.drawRoundRect(rr, 7, 7, fill);
      stroke.setColor(on ? COL_GOLD : 0xFF473D30);
      stroke.setStrokeWidth(on ? 2f : 1f);
      stroke.setStyle(Paint.Style.STROKE);
      c.drawRoundRect(rr, 7, 7, stroke);
      stroke.setStyle(Paint.Style.FILL);
      if (on) {
        fill.setColor(COL_LED);
        c.drawCircle(rr.left + 14, rr.centerY(), 4, fill);
      }
      text.setTypeface(on ? sansBold : sans);
      text.setTextSize(16);
      text.setTextAlign(Paint.Align.CENTER);
      text.setColor(on ? COL_CREAM : COL_DIM);
      c.drawText(labels[i], rr.centerX() + 6, rr.centerY() + 6, text);
    }

    // Rotor cabinet.
    tmp.set(1632, 176, 1742, 384);
    fill.setColor(0xFF2A1A10);
    c.drawRoundRect(tmp, 8, 8, fill);
    stroke.setColor(0xFF61401F);
    stroke.setStrokeWidth(2f);
    stroke.setStyle(Paint.Style.STROKE);
    c.drawRoundRect(tmp, 8, 8, stroke);
    stroke.setStyle(Paint.Style.FILL);
    fill.setColor(0xFF0B0908);
    c.drawRoundRect(new RectF(1642, 186, 1732, 374), 5, 5, fill);

    float hornCx = 1687f;
    float hornCy = 246f;
    float drumCy = 340f;
    // Top horn.
    fill.setColor(0xFF171412);
    c.drawCircle(hornCx, hornCy, 42, fill);
    stroke.setColor(0x335A4A32);
    stroke.setStrokeWidth(1f);
    for (int i = 1; i < 4; i++) {
      c.drawCircle(hornCx, hornCy, 10f * i, stroke);
    }
    stroke.setStyle(Paint.Style.FILL);
    int save = c.save();
    c.rotate(telemetry[0], hornCx, hornCy);
    fill.setColor(0xFFC8BFAE);
    tmp.set(hornCx - 4, hornCy - 6, hornCx + 40, hornCy + 6);
    c.drawRoundRect(tmp, 6, 6, fill);
    fill.setColor(0xFFF0E7D2);
    c.drawCircle(hornCx + 40, hornCy, 8, fill);
    fill.setColor(0xFF8E8576);
    c.drawCircle(hornCx - 4, hornCy, 5, fill);
    c.restoreToCount(save);

    // Bass drum.
    fill.setColor(0xFF171412);
    c.drawCircle(hornCx, drumCy, 34, fill);
    stroke.setColor(0x445A4A32);
    stroke.setStrokeWidth(1f);
    for (int i = 1; i < 3; i++) c.drawCircle(hornCx, drumCy, 11f * i, stroke);
    stroke.setStyle(Paint.Style.FILL);
    save = c.save();
    c.rotate(telemetry[1], hornCx, drumCy);
    fill.setColor(0xFF6E6353);
    for (int i = 0; i < 3; i++) {
      c.save();
      c.rotate(i * 120f, hornCx, drumCy);
      tmp.set(hornCx + 6, drumCy - 9, hornCx + 30, drumCy + 9);
      c.drawRoundRect(tmp, 4, 4, fill);
      c.restore();
    }
    c.restoreToCount(save);

    text.setTypeface(sans);
    text.setTextSize(11);
    text.setTextAlign(Paint.Align.CENTER);
    text.setColor(COL_DIM);
    c.drawText(params.leslieSpeed == 2 ? "TREMOLO" :
        (params.leslieSpeed == 1 ? "CHORALE" : "STOPPED"), 1687, 404, text);
    text.setTypeface(mono);
    text.setTextSize(11);
    text.setColor(COL_GOLD);
    c.drawText(String.format(java.util.Locale.US, "%d°", Math.round(telemetry[0]) % 360),
        1687, 420, text);
  }

  private void drawKnobs(Canvas c) {
    String[] values = new String[6];
    values[0] = percent(params.drive);
    values[1] = percent(params.keyClick);
    values[2] = percent(params.reverb);
    values[3] = percent(params.micDistance);
    values[4] = percent(params.wear);
    values[5] = percent(params.volume);
    for (int i = 0; i < 6; i++) {
      drawKnob(c, knobX[i], knobY, knobR, knobValue(i), KNOB_LABELS[i], values[i]);
    }
    // Output meter on the right edge of the strip.
    text.setTypeface(sansBold);
    text.setTextSize(11);
    text.setTextAlign(Paint.Align.RIGHT);
    text.setColor(COL_DIM);
    c.drawText("OUT", 1756, 668, text);
    tmp.set(1640, 688, 1756, 700);
    fill.setColor(0xFF0A0807);
    c.drawRoundRect(tmp, 4, 4, fill);
    tmp.set(1640, 688, 1640 + 116 * Math.min(1f, displayPeak * 2.2f), 700);
    fill.setColor(displayPeak > 0.9f ? COL_LED : COL_GOLD);
    c.drawRoundRect(tmp, 4, 4, fill);
    text.setTypeface(mono);
    text.setTextSize(10);
    text.setTextAlign(Paint.Align.RIGHT);
    text.setColor(COL_DIM);
    c.drawText((int) telemetry[4] + " NOTES", 1756, 724, text);
  }

  private void drawKnob(Canvas c, float cx, float cy, float r, float value,
                        String label, String valueText) {
    tmp.set(cx - r - 7, cy - r - 7, cx + r + 7, cy + r + 7);
    fill.setColor(0x55000000);
    c.drawOval(tmp, fill);
    fill.setColor(0xFFB4AC9C);
    c.drawCircle(cx, cy, r + 6, fill);
    fill.setColor(0xFF1A1713);
    c.drawCircle(cx, cy, r, fill);
    fill.setColor(0xFF24201B);
    c.drawCircle(cx, cy, r - 6, fill);

    stroke.setColor(0xFFD8B45A);
    stroke.setStrokeWidth(3.5f);
    stroke.setStyle(Paint.Style.STROKE);
    tmp.set(cx - r - 5, cy - r - 5, cx + r + 5, cy + r + 5);
    c.drawArc(tmp, 135f, 270f * clamp(value, 0f, 1f), false, stroke);
    stroke.setStyle(Paint.Style.FILL);

    double a = Math.toRadians(135.0 + 270.0 * clamp(value, 0f, 1f));
    float ix = (float) Math.cos(a);
    float iy = (float) Math.sin(a);
    stroke.setColor(0xFFF6EAC8);
    stroke.setStrokeWidth(3f);
    c.drawLine(cx + ix * r * 0.32f, cy + iy * r * 0.32f,
        cx + ix * r * 0.92f, cy + iy * r * 0.92f, stroke);

    text.setTypeface(sansBold);
    text.setTextSize(12);
    text.setTextAlign(Paint.Align.CENTER);
    text.setColor(COL_CREAM);
    c.drawText(label, cx, cy + r + 26, text);
    text.setTypeface(mono);
    text.setTextSize(13);
    text.setColor(COL_GOLD);
    c.drawText(valueText, cx, cy + r + 44, text);
  }

  private String percent(float v) {
    return Math.round(clamp(v, 0f, 1f) * 100f) + "%";
  }

  private void drawToggle(Canvas c, RectF r, String label, String value, boolean on) {
    fill.setColor(on ? 0xFF382B18 : 0xFF151210);
    c.drawRoundRect(r, 8, 8, fill);
    stroke.setColor(on ? COL_GOLD : 0xFF473D30);
    stroke.setStrokeWidth(on ? 2f : 1f);
    stroke.setStyle(Paint.Style.STROKE);
    c.drawRoundRect(r, 8, 8, stroke);
    stroke.setStyle(Paint.Style.FILL);
    if (on) {
      fill.setColor(COL_LED);
      c.drawCircle(r.right - 13, r.top + 13, 4f, fill);
    }
    text.setTypeface(sansBold);
    text.setTextSize(12);
    text.setTextAlign(Paint.Align.CENTER);
    text.setColor(COL_DIM);
    c.drawText(label, r.centerX(), r.top + 26, text);
    text.setTypeface(sansBold);
    text.setTextSize(19);
    text.setColor(on ? COL_CREAM : 0xFF9A8F7C);
    c.drawText(value, r.centerX(), r.top + 60, text);
  }

  private void drawKeyboard(Canvas c) {
    // Felt strip.
    fill.setColor(0xFF3B0E0C);
    c.drawRect(24, kbTop - 12, VW - 24, kbTop, fill);
    fill.setColor(0xFF6B1A15);
    c.drawRect(24, kbTop - 12, VW - 24, kbTop - 8, fill);
    fill.setColor(0x66000000);
    c.drawRect(24, kbTop - 3, VW - 24, kbTop, fill);

    // White keys.
    for (int i = 0; i < WHITE_KEYS; i++) {
      float left = whiteLeft[i];
      tmp.set(left + 0.7f, kbTop, left + whiteW - 0.7f, kbBottom);
      boolean active = heldNote(whiteNote[i]);
      fill.setShader(active ? null : whiteKeyGradient);
      fill.setColor(active ? 0xFFE4D2A6 : 0xFFF0E4C6);
      c.drawRoundRect(tmp, 2.5f, 5f, fill);
      fill.setShader(null);
      stroke.setColor(0xFF594A33);
      stroke.setStrokeWidth(0.8f);
      stroke.setStyle(Paint.Style.STROKE);
      c.drawRoundRect(tmp, 2.5f, 5f, stroke);
      stroke.setStyle(Paint.Style.FILL);

      int n = whiteNote[i];
      if (n % 12 == 0) {
        text.setTypeface(sans);
        text.setTextSize(11);
        text.setTextAlign(Paint.Align.CENTER);
        text.setColor(0xFF8C7B59);
        c.drawText("C" + (n / 12 - 1), left + whiteW * 0.5f, kbBottom - 10, text);
      }
    }

    // Black keys.
    float blackBottom = kbTop + 0.63f * (kbBottom - kbTop);
    for (int i = 0; i < BLACK_KEYS; i++) {
      float left = blackLeft[i];
      tmp.set(left, kbTop, left + blackW, blackBottom);
      boolean active = heldNote(blackNote[i]);
      fill.setShader(active ? null : blackKeyGradient);
      fill.setColor(active ? 0xFF4A3922 : 0xFF141210);
      c.drawRoundRect(tmp, 2.5f, 3.5f, fill);
      fill.setShader(null);
      fill.setColor(0x55FFFFFF);
      c.drawRect(left + 3, kbTop + 4, left + blackW * 0.42f, blackBottom - 12, fill);
      stroke.setColor(0xFF000000);
      stroke.setStrokeWidth(1.2f);
      stroke.setStyle(Paint.Style.STROKE);
      c.drawRoundRect(tmp, 2.5f, 3.5f, stroke);
      stroke.setStyle(Paint.Style.FILL);
    }

    // Lip below the keys.
    fill.setColor(0xFF23150C);
    c.drawRect(kbLeft, kbBottom, kbRight, kbBottom + 6, fill);
  }

  private void drawFooter(Canvas c) {
    text.setTypeface(sans);
    text.setTextSize(11);
    text.setTextAlign(Paint.Align.LEFT);
    text.setColor(0xFF8A7A60);
    c.drawText("C2 – C7 · 61 KEYS · 9 DRAWBARS · 91 TONEWHEELS · 2 ROTORS", 34, 996, text);
    text.setTextAlign(Paint.Align.RIGHT);
    text.setColor(0xFF6E6049);
    c.drawText("PHYSICAL MODEL · 48 kHz", 1766, 996, text);
  }

  private boolean heldNote(int note) {
    int idx = note - LOW_NOTE;
    return idx >= 0 && idx < held.length && held[idx];
  }

  // ---- Interaction --------------------------------------------------------

  @Override
  public boolean onTouchEvent(MotionEvent e) {
    final float x = (e.getX() - offX) / scale;
    final float y = (e.getY() - offY) / scale;
    final int action = e.getActionMasked();
    final int index = e.getActionIndex();
    final int id = e.getPointerId(index);

    switch (action) {
      case MotionEvent.ACTION_DOWN: {
        Log.i("B3Ui", "touch down at " + x + "," + y);
        if (y >= kbTop - 14 && y <= kbBottom + 10) {
          pressPointer(id, x, y);
        } else {
          dragPointer = id;
          handleDown(x, y);
        }
        return true;
      }
      case MotionEvent.ACTION_POINTER_DOWN: {
        if (y >= kbTop - 14 && y <= kbBottom + 10) pressPointer(id, x, y);
        return true;
      }
      case MotionEvent.ACTION_MOVE: {
        for (int i = 0; i < e.getPointerCount(); i++) {
          int pid = e.getPointerId(i);
          if (pointerNotes.indexOfKey(pid) >= 0) {
            float px = (e.getX(i) - offX) / scale;
            float py = (e.getY(i) - offY) / scale;
            pressPointer(pid, px, py);
          }
        }
        if (dragPointer >= 0) {
          int dragIndex = e.findPointerIndex(dragPointer);
          if (dragIndex >= 0) {
            handleDrag((e.getX(dragIndex) - offX) / scale,
                (e.getY(dragIndex) - offY) / scale);
          }
        }
        return true;
      }
      case MotionEvent.ACTION_UP:
      case MotionEvent.ACTION_POINTER_UP: {
        releasePointer(id);
        if (dragPointer == id) {
          dragPointer = -1;
          dragDrawbar = CTRL_NONE;
          dragKnob = CTRL_NONE;
        }
        return true;
      }
      case MotionEvent.ACTION_CANCEL: {
        releaseAll();
        dragPointer = -1;
        dragDrawbar = CTRL_NONE;
        dragKnob = CTRL_NONE;
        return true;
      }
      default:
        return true;
    }
  }

  private void handleDown(float x, float y) {
    for (int i = 0; i < presetRects.length; i++) {
      if (presetRects[i].contains(x, y)) {
        params.applyPreset(i);
        notifyParams();
        return;
      }
    }
    for (int i = 0; i < percRects.length; i++) {
      if (percRects[i].contains(x, y)) {
        togglePercussion(i);
        return;
      }
    }
    for (int i = 0; i < scannerRects.length; i++) {
      if (scannerRects[i].contains(x, y)) {
        params.scanner = SCANNER_VALUES[i];
        notifyParams();
        return;
      }
    }
    for (int i = 0; i < speedRects.length; i++) {
      if (speedRects[i].contains(x, y)) {
        params.leslieSpeed = SPEED_VALUES[i];
        notifyParams();
        return;
      }
    }
    if (drawbarHit.contains(x, y)) {
      float colW = (896f - 64f) / 9f;
      int i = (int) ((x - 64f) / colW);
      if (i >= 0 && i < 9) {
        dragDrawbar = i;
        dragStartY = y;
        setDrawbar(i, y);
      }
      return;
    }
    for (int i = 0; i < 6; i++) {
      float dx = x - knobX[i];
      float dy = y - knobY;
      if (dx * dx + dy * dy < (knobR + 20) * (knobR + 20)) {
        dragKnob = i;
        dragStartY = y;
        dragStartValue = knobValue(i);
        return;
      }
    }
  }

  private void handleDrag(float x, float y) {
    if (dragDrawbar != CTRL_NONE) {
      setDrawbar(dragDrawbar, y);
    } else if (dragKnob != CTRL_NONE) {
      float delta = (dragStartY - y) / 260f;
      setKnobValue(dragKnob, dragStartValue + delta);
    }
  }

  private void setDrawbar(int i, float y) {
    float v = (540f - y) / 360f * 8f;
    int level = Math.round(Math.max(0f, Math.min(8f, v)));
    if ((int) params.drawbars[i] != level) {
      params.drawbars[i] = level;
      notifyParams();
    }
  }

  private void togglePercussion(int which) {
    switch (which) {
      case 0: params.percussionOn = !params.percussionOn; break;
      case 1: params.percussionSoft = !params.percussionSoft; break;
      case 2: params.percussionSlow = !params.percussionSlow; break;
      default: params.percussionThird = !params.percussionThird; break;
    }
    notifyParams();
  }

  private float knobValue(int i) {
    switch (i) {
      case 0: return params.drive;
      case 1: return params.keyClick;
      case 2: return params.reverb;
      case 3: return params.micDistance;
      case 4: return params.wear;
      default: return params.volume;
    }
  }

  private void setKnobValue(int i, float v) {
    v = Math.max(0f, Math.min(1f, v));
    switch (i) {
      case 0: params.drive = v; break;
      case 1: params.keyClick = v; break;
      case 2: params.reverb = v; break;
      case 3: params.micDistance = v; break;
      case 4: params.wear = v; break;
      default: params.volume = v; break;
    }
    notifyParams();
  }

  private void notifyParams() {
    if (host != null) host.paramsChanged(params);
    invalidate();
  }

  private void pressPointer(int id, float x, float y) {
    int note = noteAt(x, y);
    if (note < 0) {
      releasePointer(id);
      return;
    }
    int previous = pointerNotes.get(id, -1);
    if (previous == note) return;
    if (previous >= 0) releaseNote(previous);
    pointerNotes.put(id, note);
    pressNote(note);
  }

  private void releasePointer(int id) {
    int previous = pointerNotes.get(id, -1);
    if (previous >= 0) {
      releaseNote(previous);
      pointerNotes.delete(id);
    }
  }

  private void releaseAll() {
    for (int i = 0; i < pointerNotes.size(); i++) {
      releaseNote(pointerNotes.valueAt(i));
    }
    pointerNotes.clear();
  }

  private void pressNote(int note) {
    int idx = note - LOW_NOTE;
    if (idx < 0 || idx >= held.length) return;
    if (!held[idx]) {
      held[idx] = true;
      if (host != null) host.noteOn(note);
      invalidate();
    }
  }

  private void releaseNote(int note) {
    int idx = note - LOW_NOTE;
    if (idx < 0 || idx >= held.length) return;
    if (held[idx]) {
      held[idx] = false;
      if (host != null) host.noteOff(note);
      invalidate();
    }
  }

  private int noteAt(float x, float y) {
    if (y < kbTop - 6 || y > kbBottom) return -1;
    float blackBottom = kbTop + 0.63f * (kbBottom - kbTop);
    if (y <= blackBottom) {
      for (int i = 0; i < BLACK_KEYS; i++) {
        if (x >= blackLeft[i] && x <= blackLeft[i] + blackW) return blackNote[i];
      }
    }
    int wi = (int) ((x - kbLeft) / whiteW);
    if (wi < 0 || wi >= WHITE_KEYS) return -1;
    return whiteNote[wi];
  }

  @Override
  protected void onDetachedFromWindow() {
    releaseAll();
    if (host != null) host.paramsChanged(params);
    super.onDetachedFromWindow();
  }

  private static float clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
  }
}
