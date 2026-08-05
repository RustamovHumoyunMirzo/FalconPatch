package dev.falconpatch.runtime;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Base64;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.ArrayAdapter;
import android.widget.GridLayout;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.Switch;
import android.webkit.WebView;
import java.io.StringReader;
import java.lang.ref.WeakReference;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserFactory;

public final class RuntimeBridge {
    private static final String TAG = "FalconPatch";
    private static final String LIBRARY_METADATA = "dev.falconpatch.library";
    private static final int MAX_EVENTS = 128;
    private static final Object EVENT_LOCK = new Object();
    private static final ArrayDeque<String> EVENTS = new ArrayDeque<String>();
    private static boolean attempted;
    private static boolean started;
    private static boolean lifecycleRegistered;
    private static WeakReference<Activity> currentActivity =
            new WeakReference<Activity>(null);
    private static View overlayView;
    private static int nextOverlayId = 1;
    private static int nextElementId = 100000;
    private static final Map<Integer, OverlayState> OVERLAYS =
            new HashMap<Integer, OverlayState>();
    private static final Map<Integer, ElementState> ELEMENTS =
            new HashMap<Integer, ElementState>();

    private RuntimeBridge() {}

    public static synchronized boolean start(Context context) {
        if (attempted) {
            return started;
        }
        attempted = true;
        try {
            Context appContext = context.getApplicationContext();
            if (appContext == null) {
                appContext = context;
            }
            registerLifecycle(appContext);
            ApplicationInfo info = appContext.getPackageManager().getApplicationInfo(
                    appContext.getPackageName(), PackageManager.GET_META_DATA);
            String library = "falconpatch";
            if (info.metaData != null) {
                String configured = info.metaData.getString(LIBRARY_METADATA);
                if (configured != null && configured.matches("[A-Za-z]+")) {
                    library = configured;
                }
            }
            System.loadLibrary(library);
            started = nativeStart(appContext);
        } catch (Throwable error) {
            Log.e(TAG, "Runtime startup failed; continuing without instrumentation.", error);
            started = false;
        }
        return started;
    }

    public static void showToast(final Context context, final String message) {
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                Toast.makeText(context.getApplicationContext(), message, Toast.LENGTH_SHORT).show();
            }
        });
    }

    public static String packageName(Context context) {
        return context == null ? null : context.getPackageName();
    }

    public static String currentActivityName() {
        Activity activity = currentActivity.get();
        return activity == null ? null : activity.getClass().getName();
    }

    public static boolean isForeground() {
        return currentActivity.get() != null;
    }

    public static String pollEvent() {
        synchronized (EVENT_LOCK) {
            return EVENTS.pollFirst();
        }
    }

    public static void emitEvent(String event) {
        pushEvent(event == null ? "custom" : event);
    }

    public static boolean showOverlay(final Context context, final String title,
                                      final String body) {
        if (context == null) {
            return false;
        }
        final Activity activity = currentActivity.get();
        if (activity == null) {
            return false;
        }
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                try {
                    clearOverlayOnMain();
                    LinearLayout panel = new LinearLayout(activity);
                    panel.setOrientation(LinearLayout.VERTICAL);
                    panel.setPadding(dp(activity, 12), dp(activity, 10),
                            dp(activity, 12), dp(activity, 10));
                    panel.setBackgroundColor(0xdd20242a);

                    TextView heading = new TextView(activity);
                    heading.setText(title == null || title.length() == 0
                            ? "FalconPatch" : title);
                    heading.setTextColor(Color.WHITE);
                    heading.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
                    heading.setGravity(Gravity.START);
                    panel.addView(heading);

                    if (body != null && body.length() > 0) {
                        TextView detail = new TextView(activity);
                        detail.setText(body);
                        detail.setTextColor(0xffd9eefc);
                        detail.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
                        panel.addView(detail);
                    }

                    FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                            ViewGroup.LayoutParams.MATCH_PARENT,
                            ViewGroup.LayoutParams.WRAP_CONTENT,
                            Gravity.TOP);
                    activity.addContentView(panel, params);
                    overlayView = panel;
                    pushEvent("ui:overlay:shown:" + activity.getClass().getName());
                } catch (Throwable error) {
                    Log.e(TAG, "Could not show overlay.", error);
                    pushEvent("ui:overlay:error");
                }
            }
        });
        return true;
    }

    public static boolean clearOverlay() {
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                clearOverlayOnMain();
            }
        });
        return true;
    }

    public static int createOverlay() {
        final Activity activity = currentActivity.get();
        if (activity == null) {
            return 0;
        }
        final int id = nextOverlayId++;
        return runOnMainSync(new MainIntTask() {
            @Override
            public int run() {
                FrameLayout overlay = new FrameLayout(activity);
                overlay.setBackgroundColor(Color.TRANSPARENT);
                overlay.setClickable(false);
                overlay.setClipChildren(false);
                overlay.setClipToPadding(false);
                FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT,
                        Gravity.TOP | Gravity.START);
                activity.addContentView(overlay, params);
                OVERLAYS.put(id, new OverlayState(id, overlay));
                pushEvent("ui:overlay:add:" + id);
                return id;
            }
        });
    }

    public static boolean setOverlayInt(final int id, final String key, final int value) {
        return runOnMainSync(new MainBooleanTask() {
            @Override
            public boolean run() {
                OverlayState overlay = OVERLAYS.get(id);
                if (overlay == null || key == null) {
                    return false;
                }
                ViewGroup.LayoutParams base = overlay.view.getLayoutParams();
                FrameLayout.LayoutParams params = base instanceof FrameLayout.LayoutParams
                        ? (FrameLayout.LayoutParams) base
                        : new FrameLayout.LayoutParams(base);
                if ("width".equals(key)) {
                    params.width = layoutSize(value);
                } else if ("height".equals(key)) {
                    params.height = layoutSize(value);
                } else if ("x".equals(key)) {
                    params.leftMargin = dp(overlay.view.getContext(), value);
                } else if ("y".equals(key)) {
                    params.topMargin = dp(overlay.view.getContext(), value);
                } else if ("gravity".equals(key)) {
                    params.gravity = value;
                } else if ("background".equals(key)) {
                    overlay.view.setBackgroundColor(value);
                } else if ("alpha".equals(key)) {
                    overlay.view.setAlpha(Math.max(0f, Math.min(1f, value / 100f)));
                } else if ("visible".equals(key)) {
                    overlay.view.setVisibility(value == 0 ? View.GONE : View.VISIBLE);
                } else {
                    return false;
                }
                overlay.view.setLayoutParams(params);
                return true;
            }
        });
    }

    public static int addElement(final int overlayId, final String type, final String text) {
        if (type == null) {
            return 0;
        }
        final int id = nextElementId++;
        return runOnMainSync(new MainIntTask() {
            @Override
            public int run() {
                final ContainerTarget target = findContainer(overlayId);
                View view;
                if (target == null) {
                    return 0;
                }
                if ("button".equals(type)) {
                    Button button = new Button(target.container.getContext());
                    button.setText(text == null ? "" : text);
                    view = button;
                } else if ("switch".equals(type)) {
                    Switch control = new Switch(target.container.getContext());
                    control.setText(text == null ? "" : text);
                    view = control;
                } else if ("checkbox".equals(type)) {
                    CheckBox checkbox = new CheckBox(target.container.getContext());
                    checkbox.setText(text == null ? "" : text);
                    view = checkbox;
                } else if ("list".equals(type)) {
                    ListView list = new ListView(target.container.getContext());
                    list.setAdapter(new ArrayAdapter<String>(target.container.getContext(),
                            android.R.layout.simple_list_item_1, splitItems(text)));
                    view = list;
                } else if ("grid".equals(type)) {
                    GridLayout grid = new GridLayout(target.container.getContext());
                    grid.setColumnCount(2);
                    view = grid;
                } else if ("hlayout".equals(type) || "vlayout".equals(type)) {
                    LinearLayout layout = new LinearLayout(target.container.getContext());
                    layout.setOrientation("hlayout".equals(type)
                            ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
                    layout.setClipChildren(false);
                    layout.setClipToPadding(false);
                    view = layout;
                } else if ("image".equals(type)) {
                    ImageView image = new ImageView(target.container.getContext());
                    image.setScaleType(ImageView.ScaleType.FIT_CENTER);
                    if (text != null && text.length() > 0) {
                        setImageSource(image, text);
                    }
                    view = image;
                } else if ("glsurface".equals(type)) {
                    GLSurfaceView surface = new GLSurfaceView(target.container.getContext());
                    surface.setPreserveEGLContextOnPause(true);
                    surface.setRenderer(new ClearRenderer());
                    view = surface;
                } else if ("webview".equals(type)) {
                    WebView webView = new WebView(target.container.getContext());
                    if (text != null && text.length() > 0) {
                        webView.loadDataWithBaseURL(null, text, "text/html", "UTF-8", null);
                    }
                    view = webView;
                } else {
                    TextView label = new TextView(target.container.getContext());
                    label.setText(text == null ? "" : text);
                    label.setTextColor(Color.WHITE);
                    label.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
                    view = label;
                }
                target.container.addView(view, defaultParams(target.container));
                ElementState element = new ElementState(id, target.overlayId, view);
                ELEMENTS.put(id, element);
                attachElementListeners(element);
                pushEvent("ui:element:add:" + id + ":" + type);
                return id;
            }
        });
    }

    public static boolean setElementInt(final int id, final String key, final int value) {
        return runOnMainSync(new MainBooleanTask() {
            @Override
            public boolean run() {
                ElementState element = ELEMENTS.get(id);
                if (element == null || key == null) {
                    return false;
                }
                ViewGroup.LayoutParams params = element.view.getLayoutParams();
                if (params == null) {
                    params = defaultParams((ViewGroup) element.view.getParent());
                }
                if ("width".equals(key)) {
                    params.width = layoutSize(value);
                } else if ("height".equals(key)) {
                    params.height = layoutSize(value);
                } else if ("x".equals(key) && params instanceof ViewGroup.MarginLayoutParams) {
                    ((ViewGroup.MarginLayoutParams) params).leftMargin =
                            dp(element.view.getContext(), value);
                } else if ("y".equals(key) && params instanceof ViewGroup.MarginLayoutParams) {
                    ((ViewGroup.MarginLayoutParams) params).topMargin =
                            dp(element.view.getContext(), value);
                } else if ("background".equals(key)) {
                    element.backgroundColor = value;
                    applyElementBackground(element);
                } else if ("text_color".equals(key) && element.view instanceof TextView) {
                    ((TextView) element.view).setTextColor(value);
                } else if ("text_size".equals(key) && element.view instanceof TextView) {
                    ((TextView) element.view).setTextSize(TypedValue.COMPLEX_UNIT_SP, value);
                } else if ("corner_radius".equals(key)) {
                    element.cornerRadius = dp(element.view.getContext(), value);
                    applyElementBackground(element);
                } else if ("padding".equals(key)) {
                    int px = dp(element.view.getContext(), value);
                    element.view.setPadding(px, px, px, px);
                } else if ("stroke_width".equals(key)) {
                    element.strokeWidth = dp(element.view.getContext(), value);
                    applyElementBackground(element);
                } else if ("stroke_color".equals(key)) {
                    element.strokeColor = value;
                    applyElementBackground(element);
                } else if ("columns".equals(key) && element.view instanceof GridLayout) {
                    ((GridLayout) element.view).setColumnCount(Math.max(1, value));
                } else if ("visible".equals(key)) {
                    element.view.setVisibility(value == 0 ? View.GONE : View.VISIBLE);
                } else {
                    return false;
                }
                element.view.setLayoutParams(params);
                return true;
            }
        });
    }

    public static boolean setElementString(final int id, final String key, final String value) {
        return runOnMainSync(new MainBooleanTask() {
            @Override
            public boolean run() {
                ElementState element = ELEMENTS.get(id);
                if (element == null || key == null) {
                    return false;
                }
                String actual = value == null ? "" : value;
                if ("text".equals(key) && element.view instanceof TextView) {
                    ((TextView) element.view).setText(actual);
                    return true;
                }
                if ("html".equals(key) && element.view instanceof WebView) {
                    ((WebView) element.view).loadDataWithBaseURL(null, actual,
                            "text/html", "UTF-8", null);
                    return true;
                }
                if ("items".equals(key) && element.view instanceof ListView) {
                    ((ListView) element.view).setAdapter(new ArrayAdapter<String>(
                            element.view.getContext(), android.R.layout.simple_list_item_1,
                            splitItems(actual)));
                    return true;
                }
                if ("image".equals(key) && element.view instanceof ImageView) {
                    return setImageSource((ImageView) element.view, actual);
                }
                if ("url".equals(key) && element.view instanceof WebView) {
                    ((WebView) element.view).loadUrl(actual);
                    return true;
                }
                return false;
            }
        });
    }

    public static boolean setElementBoolean(final int id, final String key, final boolean value) {
        return runOnMainSync(new MainBooleanTask() {
            @Override
            public boolean run() {
                ElementState element = ELEMENTS.get(id);
                if (element == null || key == null) {
                    return false;
                }
                if ("checked".equals(key) && element.view instanceof CheckBox) {
                    ((CheckBox) element.view).setChecked(value);
                    return true;
                }
                if ("checked".equals(key) && element.view instanceof Switch) {
                    ((Switch) element.view).setChecked(value);
                    return true;
                }
                if ("enabled".equals(key)) {
                    element.view.setEnabled(value);
                    return true;
                }
                if ("draggable".equals(key)) {
                    element.draggable = value;
                    return true;
                }
                return false;
            }
        });
    }

    public static String getElementString(int id, String key) {
        ElementState element = ELEMENTS.get(id);
        if (element == null || key == null) {
            return null;
        }
        if ("text".equals(key) && element.view instanceof TextView) {
            return ((TextView) element.view).getText().toString();
        }
        return null;
    }

    public static boolean getElementBoolean(int id, String key) {
        ElementState element = ELEMENTS.get(id);
        if (element == null || key == null) {
            return false;
        }
        if ("checked".equals(key) && element.view instanceof CheckBox) {
            return ((CheckBox) element.view).isChecked();
        }
        if ("checked".equals(key) && element.view instanceof Switch) {
            return ((Switch) element.view).isChecked();
        }
        if ("enabled".equals(key)) {
            return element.view.isEnabled();
        }
        if ("visible".equals(key)) {
            return element.view.getVisibility() == View.VISIBLE;
        }
        if ("draggable".equals(key)) {
            return element.draggable;
        }
        return false;
    }

    public static int getElementInt(int id, String key) {
        ElementState element = ELEMENTS.get(id);
        if (element == null || key == null) {
            return 0;
        }
        if ("x".equals(key)) {
            return element.view.getLeft();
        }
        if ("y".equals(key)) {
            return element.view.getTop();
        }
        if ("width".equals(key)) {
            return element.view.getWidth();
        }
        if ("height".equals(key)) {
            return element.view.getHeight();
        }
        if ("stroke_width".equals(key)) {
            return element.strokeWidth;
        }
        if ("stroke_color".equals(key)) {
            return element.strokeColor;
        }
        return 0;
    }

    public static boolean setElementFloat(final int id, final String key, final float value) {
        return runOnMainSync(new MainBooleanTask() {
            @Override
            public boolean run() {
                ElementState element = ELEMENTS.get(id);
                if (element == null || key == null) {
                    return false;
                }
                if ("weight".equals(key)) {
                    ViewGroup.LayoutParams base = element.view.getLayoutParams();
                    if (base instanceof LinearLayout.LayoutParams) {
                        LinearLayout.LayoutParams params = (LinearLayout.LayoutParams) base;
                        params.weight = value;
                        if (params.width == ViewGroup.LayoutParams.WRAP_CONTENT && value > 0) {
                            params.width = 0;
                        }
                        element.view.setLayoutParams(params);
                        element.weight = value;
                        return true;
                    }
                    element.weight = value;
                    return false;
                }
                return false;
            }
        });
    }

    public static float getElementFloat(int id, String key) {
        ElementState element = ELEMENTS.get(id);
        if (element == null || key == null) {
            return 0f;
        }
        if ("weight".equals(key)) {
            return element.weight;
        }
        return 0f;
    }

    public static boolean setElementEvent(final int id, final String kind,
                                          final String eventName) {
        return runOnMainSync(new MainBooleanTask() {
            @Override
            public boolean run() {
                ElementState element = ELEMENTS.get(id);
                if (element == null || kind == null) {
                    return false;
                }
                String actual = eventName == null || eventName.length() == 0
                        ? "ui:" + kind + ":" + id : eventName;
                element.events.put(kind, actual);
                return true;
            }
        });
    }

    public static boolean clearOverlayById(final int id) {
        return runOnMainSync(new MainBooleanTask() {
            @Override
            public boolean run() {
                OverlayState overlay = OVERLAYS.get(id);
                if (overlay == null) {
                    return false;
                }
                overlay.view.removeAllViews();
                removeElementsForOverlay(id);
                pushEvent("ui:overlay:clear:" + id);
                return true;
            }
        });
    }

    public static boolean removeOverlayById(final int id) {
        return runOnMainSync(new MainBooleanTask() {
            @Override
            public boolean run() {
                OverlayState overlay = OVERLAYS.remove(id);
                if (overlay == null) {
                    return false;
                }
                ViewGroup parent = (ViewGroup) overlay.view.getParent();
                if (parent != null) {
                    parent.removeView(overlay.view);
                }
                removeElementsForOverlay(id);
                pushEvent("ui:overlay:remove:" + id);
                return true;
            }
        });
    }

    public static int screenWidth() {
        Context context = currentContext();
        return context == null ? 0 : context.getResources().getDisplayMetrics().widthPixels;
    }

    public static int screenHeight() {
        Context context = currentContext();
        return context == null ? 0 : context.getResources().getDisplayMetrics().heightPixels;
    }

    public static float density() {
        Context context = currentContext();
        return context == null ? 0f : context.getResources().getDisplayMetrics().density;
    }

    public static float refreshRate() {
        Activity activity = currentActivity.get();
        if (activity == null || activity.getWindowManager() == null ||
                activity.getWindowManager().getDefaultDisplay() == null) {
            return 0f;
        }
        return activity.getWindowManager().getDefaultDisplay().getRefreshRate();
    }

    public static boolean inflateXmlOverlay(final Context context, final String xml) {
        if (context == null || xml == null || xml.length() == 0) {
            return false;
        }
        final Activity activity = currentActivity.get();
        if (activity == null) {
            return false;
        }
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                try {
                    XmlPullParserFactory factory = XmlPullParserFactory.newInstance();
                    factory.setNamespaceAware(true);
                    XmlPullParser parser = factory.newPullParser();
                    parser.setInput(new StringReader(xml));
                    View inflated = LayoutInflater.from(activity).inflate(parser, null);
                    FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                            ViewGroup.LayoutParams.MATCH_PARENT,
                            ViewGroup.LayoutParams.WRAP_CONTENT,
                            Gravity.TOP);
                    clearOverlayOnMain();
                    activity.addContentView(inflated, params);
                    overlayView = inflated;
                    pushEvent("ui:xml:inflated:" + activity.getClass().getName());
                } catch (Throwable error) {
                    Log.e(TAG, "Could not inflate XML overlay.", error);
                    pushEvent("ui:xml:error");
                }
            }
        });
        return true;
    }

    public static String inspectCurrentUi() {
        Activity activity = currentActivity.get();
        if (activity == null) {
            return "activity=null\n";
        }
        View root = activity.getWindow() == null ? null
                : activity.getWindow().getDecorView();
        StringBuilder out = new StringBuilder(8192);
        out.append("activity=").append(activity.getClass().getName()).append('\n');
        if (root != null) {
            appendView(out, root, 0);
        }
        return out.toString();
    }

    public static boolean startActivityIntent(Context context, String action,
                                              String uri, String packageName) {
        if (context == null || action == null || action.length() == 0) {
            return false;
        }
        try {
            Intent intent = new Intent(action);
            if (uri != null && uri.length() > 0) {
                intent.setData(Uri.parse(uri));
            }
            if (packageName != null && packageName.length() > 0) {
                intent.setPackage(packageName);
            }
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(intent);
            pushEvent("intent:start:" + action);
            return true;
        } catch (Throwable error) {
            Log.e(TAG, "Could not start activity intent.", error);
            pushEvent("intent:start:error:" + action);
            return false;
        }
    }

    public static boolean sendBroadcastIntent(Context context, String action,
                                              String uri, String packageName) {
        if (context == null || action == null || action.length() == 0) {
            return false;
        }
        try {
            Intent intent = new Intent(action);
            if (uri != null && uri.length() > 0) {
                intent.setData(Uri.parse(uri));
            }
            if (packageName != null && packageName.length() > 0) {
                intent.setPackage(packageName);
            }
            context.sendBroadcast(intent);
            pushEvent("intent:broadcast:" + action);
            return true;
        } catch (Throwable error) {
            Log.e(TAG, "Could not send broadcast intent.", error);
            pushEvent("intent:broadcast:error:" + action);
            return false;
        }
    }

    private static void registerLifecycle(Context context) {
        if (lifecycleRegistered) {
            return;
        }
        Context appContext = context.getApplicationContext();
        if (!(appContext instanceof Application)) {
            return;
        }
        ((Application) appContext).registerActivityLifecycleCallbacks(
                new Application.ActivityLifecycleCallbacks() {
                    @Override
                    public void onActivityCreated(Activity activity, Bundle state) {
                        pushEvent("activity:created:" + activity.getClass().getName());
                    }

                    @Override
                    public void onActivityStarted(Activity activity) {
                        currentActivity = new WeakReference<Activity>(activity);
                        pushEvent("activity:started:" + activity.getClass().getName());
                    }

                    @Override
                    public void onActivityResumed(Activity activity) {
                        currentActivity = new WeakReference<Activity>(activity);
                        pushEvent("activity:resumed:" + activity.getClass().getName());
                    }

                    @Override
                    public void onActivityPaused(Activity activity) {
                        pushEvent("activity:paused:" + activity.getClass().getName());
                    }

                    @Override
                    public void onActivityStopped(Activity activity) {
                        Activity current = currentActivity.get();
                        if (current == activity) {
                            currentActivity = new WeakReference<Activity>(null);
                        }
                        pushEvent("activity:stopped:" + activity.getClass().getName());
                    }

                    @Override
                    public void onActivitySaveInstanceState(Activity activity, Bundle state) {}

                    @Override
                    public void onActivityDestroyed(Activity activity) {
                        pushEvent("activity:destroyed:" + activity.getClass().getName());
                    }
                });
        lifecycleRegistered = true;
    }

    private static void clearOverlayOnMain() {
        if (overlayView == null) {
            return;
        }
        ViewGroup parent = (ViewGroup) overlayView.getParent();
        if (parent != null) {
            parent.removeView(overlayView);
        }
        overlayView = null;
        pushEvent("ui:overlay:cleared");
    }

    private static void appendView(StringBuilder out, View view, int depth) {
        int i;
        for (i = 0; i < depth; i++) {
            out.append("  ");
        }
        out.append(view.getClass().getName())
                .append(" id=").append(view.getId())
                .append(" visible=").append(view.getVisibility() == View.VISIBLE)
                .append(" enabled=").append(view.isEnabled())
                .append(" size=").append(view.getWidth()).append('x').append(view.getHeight());
        if (view instanceof TextView) {
            CharSequence text = ((TextView) view).getText();
            if (text != null && text.length() > 0) {
                out.append(" text=\"").append(escape(text.toString(), 96)).append('"');
            }
        }
        out.append('\n');
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            int count = Math.min(group.getChildCount(), 256);
            for (i = 0; i < count; i++) {
                appendView(out, group.getChildAt(i), depth + 1);
            }
        }
    }

    private static String escape(String value, int limit) {
        StringBuilder out = new StringBuilder();
        int count = Math.min(value.length(), limit);
        for (int i = 0; i < count; i++) {
            char ch = value.charAt(i);
            if (ch == '\n' || ch == '\r' || ch == '\t') {
                out.append(' ');
            } else if (ch == '"') {
                out.append('\'');
            } else {
                out.append(ch);
            }
        }
        if (value.length() > limit) {
            out.append("...");
        }
        return out.toString();
    }

    private static int dp(Context context, int value) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP,
                value, context.getResources().getDisplayMetrics());
    }

    private static int layoutSize(int value) {
        if (value == -1 || value == -2) {
            return value;
        }
        Context context = currentContext();
        return context == null ? value : dp(context, value);
    }

    private static Context currentContext() {
        Activity activity = currentActivity.get();
        if (activity != null) {
            return activity;
        }
        return null;
    }

    private static void applyElementBackground(ElementState element) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(element.backgroundColor);
        drawable.setCornerRadius(element.cornerRadius);
        if (element.strokeWidth > 0) {
            drawable.setStroke(element.strokeWidth, element.strokeColor);
        }
        element.view.setBackground(drawable);
    }

    private static ContainerTarget findContainer(int id) {
        OverlayState overlay = OVERLAYS.get(id);
        if (overlay != null) {
            return new ContainerTarget(overlay.id, overlay.view);
        }
        ElementState element = ELEMENTS.get(id);
        if (element != null && element.view instanceof ViewGroup) {
            return new ContainerTarget(element.overlayId, (ViewGroup) element.view);
        }
        return null;
    }

    private static ViewGroup.LayoutParams defaultParams(ViewGroup parent) {
        if (parent instanceof LinearLayout) {
            return new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT);
        }
        if (parent instanceof GridLayout) {
            return new GridLayout.LayoutParams();
        }
        return new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.START);
    }

    private static List<String> splitItems(String text) {
        ArrayList<String> items = new ArrayList<String>();
        if (text == null || text.length() == 0) {
            return items;
        }
        String[] parts = text.split("\\r?\\n|,");
        for (String part : parts) {
            String trimmed = part.trim();
            if (trimmed.length() > 0) {
                items.add(trimmed);
            }
        }
        return items;
    }

    private static boolean setImageSource(ImageView image, String source) {
        try {
            if (source.startsWith("data:image")) {
                int comma = source.indexOf(',');
                if (comma >= 0) {
                    byte[] data = Base64.decode(source.substring(comma + 1), Base64.DEFAULT);
                    Bitmap bitmap = BitmapFactory.decodeByteArray(data, 0, data.length);
                    image.setImageBitmap(bitmap);
                    return bitmap != null;
                }
            }
            image.setImageURI(Uri.parse(source));
            return true;
        } catch (Throwable error) {
            Log.e(TAG, "Could not set image source.", error);
            return false;
        }
    }

    private static void attachElementListeners(final ElementState element) {
        element.view.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                if (view instanceof CheckBox) {
                    pushEvent("ui:checked:" + element.id + ":" +
                            ((CheckBox) view).isChecked());
                }
                pushElementEvent(element, "click", "");
            }
        });
        element.view.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View view, android.view.MotionEvent event) {
                String suffix = ":" + (int) event.getRawX() + ":" + (int) event.getRawY();
                if (event.getAction() == android.view.MotionEvent.ACTION_DOWN) {
                    element.dragStartRawX = event.getRawX();
                    element.dragStartRawY = event.getRawY();
                    element.dragStartLeft = view.getLeft();
                    element.dragStartTop = view.getTop();
                    pushElementEvent(element, "down", suffix);
                } else if (event.getAction() == android.view.MotionEvent.ACTION_MOVE) {
                    if (element.draggable) {
                        ViewGroup.LayoutParams base = view.getLayoutParams();
                        if (base instanceof FrameLayout.LayoutParams) {
                            FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) base;
                            params.leftMargin = element.dragStartLeft +
                                    (int) (event.getRawX() - element.dragStartRawX);
                            params.topMargin = element.dragStartTop +
                                    (int) (event.getRawY() - element.dragStartRawY);
                            view.setLayoutParams(params);
                        }
                    }
                    pushElementEvent(element, "drag", suffix);
                } else if (event.getAction() == android.view.MotionEvent.ACTION_UP ||
                        event.getAction() == android.view.MotionEvent.ACTION_CANCEL) {
                    pushElementEvent(element, "up", suffix);
                }
                return element.draggable;
            }
        });
        element.view.setOnHoverListener(new View.OnHoverListener() {
            @Override
            public boolean onHover(View view, android.view.MotionEvent event) {
                if (event.getAction() == android.view.MotionEvent.ACTION_HOVER_ENTER ||
                        event.getAction() == android.view.MotionEvent.ACTION_HOVER_MOVE ||
                        event.getAction() == android.view.MotionEvent.ACTION_HOVER_EXIT) {
                    pushElementEvent(element, "hover",
                            ":" + (int) event.getX() + ":" + (int) event.getY());
                }
                return false;
            }
        });
    }

    private static void pushElementEvent(ElementState element, String kind, String suffix) {
        String event = element.events.get(kind);
        if (event == null) {
            event = "ui:" + kind + ":" + element.id;
        }
        pushEvent(event + (suffix == null ? "" : suffix));
    }

    private static void removeElementsForOverlay(int overlayId) {
        Integer[] keys = ELEMENTS.keySet().toArray(new Integer[ELEMENTS.size()]);
        for (Integer key : keys) {
            ElementState element = ELEMENTS.get(key);
            if (element != null && element.overlayId == overlayId) {
                ELEMENTS.remove(key);
            }
        }
    }

    private interface MainBooleanTask {
        boolean run();
    }

    private interface MainIntTask {
        int run();
    }

    private static boolean runOnMainSync(final MainBooleanTask task) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            return task.run();
        }
        final boolean[] result = new boolean[] { false };
        final CountDownLatch latch = new CountDownLatch(1);
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                try {
                    result[0] = task.run();
                } finally {
                    latch.countDown();
                }
            }
        });
        try {
            latch.await();
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
        return result[0];
    }

    private static int runOnMainSync(final MainIntTask task) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            return task.run();
        }
        final int[] result = new int[] { 0 };
        final CountDownLatch latch = new CountDownLatch(1);
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                try {
                    result[0] = task.run();
                } finally {
                    latch.countDown();
                }
            }
        });
        try {
            latch.await();
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
        return result[0];
    }

    private static final class OverlayState {
        final int id;
        final FrameLayout view;

        OverlayState(int id, FrameLayout view) {
            this.id = id;
            this.view = view;
        }
    }

    private static final class ContainerTarget {
        final int overlayId;
        final ViewGroup container;

        ContainerTarget(int overlayId, ViewGroup container) {
            this.overlayId = overlayId;
            this.container = container;
        }
    }

    private static final class ClearRenderer implements GLSurfaceView.Renderer {
        @Override
        public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            gl.glClearColor(0f, 0f, 0f, 0f);
        }

        @Override
        public void onSurfaceChanged(GL10 gl, int width, int height) {
            gl.glViewport(0, 0, width, height);
        }

        @Override
        public void onDrawFrame(GL10 gl) {
            gl.glClear(GL10.GL_COLOR_BUFFER_BIT);
        }
    }

    private static final class ElementState {
        final int id;
        final int overlayId;
        final View view;
        final Map<String, String> events = new HashMap<String, String>();
        int backgroundColor = Color.TRANSPARENT;
        int cornerRadius;
        int strokeWidth;
        int strokeColor = Color.TRANSPARENT;
        float weight;
        boolean draggable;
        float dragStartRawX;
        float dragStartRawY;
        int dragStartLeft;
        int dragStartTop;

        ElementState(int id, int overlayId, View view) {
            this.id = id;
            this.overlayId = overlayId;
            this.view = view;
        }
    }

    private static void pushEvent(String event) {
        synchronized (EVENT_LOCK) {
            if (EVENTS.size() >= MAX_EVENTS) {
                EVENTS.removeFirst();
            }
            EVENTS.addLast(event);
        }
    }

    private static native boolean nativeStart(Context context);
}
