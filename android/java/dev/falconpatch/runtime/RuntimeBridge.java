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
import java.io.InputStream;
import java.io.StringReader;
import java.lang.ref.WeakReference;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
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
    private static Context applicationContext;
    private static WeakReference<Activity> currentActivity =
            new WeakReference<Activity>(null);
    private static View overlayView;
    private static String pendingOverlayTitle;
    private static String pendingOverlayBody;
    private static int nextOverlayId = 1;
    private static int nextElementId = 100000;
    private static int nextReflectId = 1;
    private static final Map<Integer, OverlayState> OVERLAYS =
            new HashMap<Integer, OverlayState>();
    private static final Map<Integer, ElementState> ELEMENTS =
            new HashMap<Integer, ElementState>();
    private static final Map<Integer, Object> REFLECT_OBJECTS =
            new HashMap<Integer, Object>();

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
            applicationContext = appContext;
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

    public static String reflectClassExists(String className, int unusedObjectId,
                                            String unusedMember, String unusedSignature,
                                            String[] unusedArgs, String unusedValue) {
        try {
            resolveClass(className);
            return "Z:true";
        } catch (Throwable error) {
            return "Z:false";
        }
    }

    public static String reflectStatic(String className, int unusedObjectId, String member,
                                       String signature, String[] args, String unusedValue) {
        try {
            Class<?> target = resolveClass(className);
            Class<?>[] parameters = parseParameterTypes(signature);
            Method method = findMethod(target, member, parameters, true);
            Object result = method.invoke(null, convertArgs(parameters, args));
            return encodeResult(result, returnType(signature));
        } catch (Throwable error) {
            return encodeError(error);
        }
    }

    public static String reflectNew(String className, int unusedObjectId, String unusedMember,
                                    String signature, String[] args, String unusedValue) {
        try {
            Class<?> target = resolveClass(className);
            Class<?>[] parameters = parseParameterTypes(signature);
            Constructor<?> constructor = target.getDeclaredConstructor(parameters);
            constructor.setAccessible(true);
            return encodeResult(constructor.newInstance(convertArgs(parameters, args)), target);
        } catch (Throwable error) {
            return encodeError(error);
        }
    }

    public static String reflectObject(String unusedClassName, int objectId, String member,
                                       String signature, String[] args, String unusedValue) {
        try {
            Object target = REFLECT_OBJECTS.get(objectId);
            if (target == null) {
                return "E:object handle not found";
            }
            Class<?>[] parameters = parseParameterTypes(signature);
            Method method = findMethod(target.getClass(), member, parameters, false);
            Object result = method.invoke(target, convertArgs(parameters, args));
            return encodeResult(result, returnType(signature));
        } catch (Throwable error) {
            return encodeError(error);
        }
    }

    public static String reflectGetStatic(String className, int unusedObjectId, String member,
                                          String signature, String[] unusedArgs,
                                          String unusedValue) {
        try {
            Field field = findField(resolveClass(className), member, true);
            return encodeResult(field.get(null), signatureType(signature, field.getType()));
        } catch (Throwable error) {
            return encodeError(error);
        }
    }

    public static String reflectSetStatic(String className, int unusedObjectId, String member,
                                          String signature, String[] unusedArgs, String value) {
        try {
            Field field = findField(resolveClass(className), member, true);
            Class<?> type = signatureType(signature, field.getType());
            field.set(null, convertArg(type, value));
            return "Z:true";
        } catch (Throwable error) {
            return encodeError(error);
        }
    }

    public static String reflectGetObject(String unusedClassName, int objectId, String member,
                                          String signature, String[] unusedArgs,
                                          String unusedValue) {
        try {
            Object target = REFLECT_OBJECTS.get(objectId);
            if (target == null) {
                return "E:object handle not found";
            }
            Field field = findField(target.getClass(), member, false);
            return encodeResult(field.get(target), signatureType(signature, field.getType()));
        } catch (Throwable error) {
            return encodeError(error);
        }
    }

    public static String reflectSetObject(String unusedClassName, int objectId, String member,
                                          String signature, String[] unusedArgs, String value) {
        try {
            Object target = REFLECT_OBJECTS.get(objectId);
            if (target == null) {
                return "E:object handle not found";
            }
            Field field = findField(target.getClass(), member, false);
            Class<?> type = signatureType(signature, field.getType());
            field.set(target, convertArg(type, value));
            return "Z:true";
        } catch (Throwable error) {
            return encodeError(error);
        }
    }

    public static String reflectRelease(String unusedClassName, int objectId, String unusedMember,
                                        String unusedSignature, String[] unusedArgs,
                                        String unusedValue) {
        synchronized (REFLECT_OBJECTS) {
            REFLECT_OBJECTS.remove(objectId);
        }
        return "Z:true";
    }

    public static boolean showOverlay(final Context context, final String title,
                                      final String body) {
        if (context == null) {
            return false;
        }
        final Activity activity = currentActivity.get();
        if (activity == null) {
            synchronized (RuntimeBridge.class) {
                pendingOverlayTitle = title;
                pendingOverlayBody = body;
            }
            pushEvent("ui:overlay:queued");
            return true;
        }
        showOverlayOnActivity(activity, title, body);
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
        final int id = nextOverlayId++;
        final OverlayState state = new OverlayState(id);
        OVERLAYS.put(id, state);
        if (activity == null) {
            pushEvent("ui:overlay:queued:" + id);
            return id;
        }
        return runOnMainSync(new MainIntTask() {
            @Override
            public int run() {
                attachOverlayOnMain(activity, state);
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
                if ("width".equals(key)) {
                    overlay.width = value;
                } else if ("height".equals(key)) {
                    overlay.height = value;
                } else if ("x".equals(key)) {
                    overlay.x = value;
                } else if ("y".equals(key)) {
                    overlay.y = value;
                } else if ("gravity".equals(key)) {
                    overlay.gravity = value;
                } else if ("background".equals(key)) {
                    overlay.backgroundColor = value;
                } else if ("alpha".equals(key)) {
                    overlay.alpha = Math.max(0f, Math.min(1f, value / 100f));
                } else if ("visible".equals(key)) {
                    overlay.visible = value != 0;
                } else {
                    return false;
                }
                if (overlay.view != null) {
                    overlay.view.setLayoutParams(overlayParams(overlay.view.getContext(), overlay));
                    applyOverlayVisuals(overlay);
                }
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
                        loadWebView(webView, text);
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
                    loadWebView((WebView) element.view, actual);
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
                if (overlay.view != null) {
                    overlay.view.removeAllViews();
                }
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
                if (overlay.view != null) {
                    ViewGroup parent = (ViewGroup) overlay.view.getParent();
                    if (parent != null) {
                        parent.removeView(overlay.view);
                    }
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
                        attachPendingOverlays(activity);
                        flushPendingOverlay(activity);
                    }

                    @Override
                    public void onActivityResumed(Activity activity) {
                        currentActivity = new WeakReference<Activity>(activity);
                        pushEvent("activity:resumed:" + activity.getClass().getName());
                        attachPendingOverlays(activity);
                        flushPendingOverlay(activity);
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

    private static void flushPendingOverlay(Activity activity) {
        final String title;
        final String body;
        synchronized (RuntimeBridge.class) {
            if (pendingOverlayTitle == null && pendingOverlayBody == null) {
                return;
            }
            title = pendingOverlayTitle;
            body = pendingOverlayBody;
            pendingOverlayTitle = null;
            pendingOverlayBody = null;
        }
        showOverlayOnActivity(activity, title, body);
    }

    private static void showOverlayOnActivity(final Activity activity, final String title,
                                              final String body) {
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
    }

    private static void attachPendingOverlays(Activity activity) {
        for (OverlayState overlay : new ArrayList<OverlayState>(OVERLAYS.values())) {
            if (overlay.view == null) {
                attachOverlayOnMain(activity, overlay);
                pushEvent("ui:overlay:add:" + overlay.id);
            }
        }
    }

    private static void attachOverlayOnMain(Activity activity, OverlayState state) {
        FrameLayout overlay = new FrameLayout(activity);
        overlay.setClickable(false);
        overlay.setClipChildren(false);
        overlay.setClipToPadding(false);
        activity.addContentView(overlay, overlayParams(activity, state));
        state.view = overlay;
        applyOverlayVisuals(state);
    }

    private static FrameLayout.LayoutParams overlayParams(Context context, OverlayState state) {
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                layoutSize(state.width),
                layoutSize(state.height),
                state.gravity);
        params.leftMargin = dp(context, state.x);
        params.topMargin = dp(context, state.y);
        return params;
    }

    private static void applyOverlayVisuals(OverlayState state) {
        if (state.view == null) {
            return;
        }
        state.view.setBackgroundColor(state.backgroundColor);
        state.view.setAlpha(state.alpha);
        state.view.setVisibility(state.visible ? View.VISIBLE : View.GONE);
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

    private static Class<?> resolveClass(String name) throws ClassNotFoundException {
        if (name == null || name.length() == 0) {
            throw new ClassNotFoundException("empty class name");
        }
        if ("boolean".equals(name)) {
            return boolean.class;
        }
        if ("byte".equals(name)) {
            return byte.class;
        }
        if ("char".equals(name)) {
            return char.class;
        }
        if ("short".equals(name)) {
            return short.class;
        }
        if ("int".equals(name)) {
            return int.class;
        }
        if ("long".equals(name)) {
            return long.class;
        }
        if ("float".equals(name)) {
            return float.class;
        }
        if ("double".equals(name)) {
            return double.class;
        }
        if ("void".equals(name)) {
            return void.class;
        }
        ClassLoader loader = RuntimeBridge.class.getClassLoader();
        Context context = applicationContext;
        if (context == null) {
            context = currentContext();
        }
        if (context != null && context.getClassLoader() != null) {
            loader = context.getClassLoader();
        }
        return Class.forName(name.replace('/', '.'), false, loader);
    }

    private static Class<?>[] parseParameterTypes(String signature)
            throws ClassNotFoundException {
        if (signature == null || signature.length() == 0) {
            return new Class<?>[0];
        }
        int start = signature.indexOf('(');
        int end = signature.indexOf(')');
        if (start < 0 || end < start) {
            return new Class<?>[0];
        }
        ArrayList<Class<?>> types = new ArrayList<Class<?>>();
        int[] index = new int[] { start + 1 };
        while (index[0] < end) {
            types.add(parseType(signature, index));
        }
        return types.toArray(new Class<?>[types.size()]);
    }

    private static Class<?> returnType(String signature) throws ClassNotFoundException {
        if (signature == null) {
            return Object.class;
        }
        int end = signature.indexOf(')');
        if (end < 0 || end + 1 >= signature.length()) {
            return Object.class;
        }
        int[] index = new int[] { end + 1 };
        return parseType(signature, index);
    }

    private static Class<?> signatureType(String signature, Class<?> fallback)
            throws ClassNotFoundException {
        if (signature == null || signature.length() == 0) {
            return fallback == null ? Object.class : fallback;
        }
        int[] index = new int[] { 0 };
        return parseType(signature, index);
    }

    private static Class<?> parseType(String signature, int[] index)
            throws ClassNotFoundException {
        char descriptor = signature.charAt(index[0]++);
        if (descriptor == 'Z') {
            return boolean.class;
        }
        if (descriptor == 'B') {
            return byte.class;
        }
        if (descriptor == 'C') {
            return char.class;
        }
        if (descriptor == 'S') {
            return short.class;
        }
        if (descriptor == 'I') {
            return int.class;
        }
        if (descriptor == 'J') {
            return long.class;
        }
        if (descriptor == 'F') {
            return float.class;
        }
        if (descriptor == 'D') {
            return double.class;
        }
        if (descriptor == 'V') {
            return void.class;
        }
        if (descriptor == 'L') {
            int semicolon = signature.indexOf(';', index[0]);
            if (semicolon < 0) {
                throw new ClassNotFoundException("bad object descriptor");
            }
            String name = signature.substring(index[0], semicolon).replace('/', '.');
            index[0] = semicolon + 1;
            return resolveClass(name);
        }
        if (descriptor == '[') {
            int start = index[0] - 1;
            parseType(signature, index);
            return Class.forName(signature.substring(start, index[0]).replace('/', '.'));
        }
        throw new ClassNotFoundException("bad descriptor: " + descriptor);
    }

    private static Method findMethod(Class<?> type, String name, Class<?>[] parameters,
                                     boolean requireStatic) throws NoSuchMethodException {
        Class<?> cursor = type;
        while (cursor != null) {
            try {
                Method method = cursor.getDeclaredMethod(name, parameters);
                boolean isStatic = java.lang.reflect.Modifier.isStatic(method.getModifiers());
                if (isStatic == requireStatic) {
                    method.setAccessible(true);
                    return method;
                }
            } catch (NoSuchMethodException ignored) {}
            cursor = cursor.getSuperclass();
        }
        throw new NoSuchMethodException(name);
    }

    private static Field findField(Class<?> type, String name, boolean requireStatic)
            throws NoSuchFieldException {
        Class<?> cursor = type;
        while (cursor != null) {
            try {
                Field field = cursor.getDeclaredField(name);
                boolean isStatic = java.lang.reflect.Modifier.isStatic(field.getModifiers());
                if (isStatic == requireStatic) {
                    field.setAccessible(true);
                    return field;
                }
            } catch (NoSuchFieldException ignored) {}
            cursor = cursor.getSuperclass();
        }
        throw new NoSuchFieldException(name);
    }

    private static Object[] convertArgs(Class<?>[] parameterTypes, String[] raw) {
        Object[] args = new Object[parameterTypes.length];
        for (int i = 0; i < parameterTypes.length; i++) {
            String value = raw != null && i < raw.length ? raw[i] : "";
            args[i] = convertArg(parameterTypes[i], value);
        }
        return args;
    }

    private static Object convertArg(Class<?> type, String raw) {
        String value = raw == null ? "" : raw;
        if (type == String.class || type == CharSequence.class || type == Object.class) {
            return value;
        }
        if (type == boolean.class || type == Boolean.class) {
            return Boolean.valueOf("true".equalsIgnoreCase(value) || "1".equals(value));
        }
        if (type == byte.class || type == Byte.class) {
            return Byte.valueOf(value.length() == 0 ? "0" : value);
        }
        if (type == char.class || type == Character.class) {
            return Character.valueOf(value.length() == 0 ? '\0' : value.charAt(0));
        }
        if (type == short.class || type == Short.class) {
            return Short.valueOf(value.length() == 0 ? "0" : value);
        }
        if (type == int.class || type == Integer.class) {
            return Integer.valueOf(value.length() == 0 ? "0" : value);
        }
        if (type == long.class || type == Long.class) {
            return Long.valueOf(value.length() == 0 ? "0" : value);
        }
        if (type == float.class || type == Float.class) {
            return Float.valueOf(value.length() == 0 ? "0" : value);
        }
        if (type == double.class || type == Double.class) {
            return Double.valueOf(value.length() == 0 ? "0" : value);
        }
        if (value.startsWith("@")) {
            try {
                Object object = REFLECT_OBJECTS.get(Integer.valueOf(value.substring(1)));
                if (object == null || !type.isInstance(object)) {
                    return null;
                }
                return object;
            } catch (NumberFormatException ignored) {}
        }
        return null;
    }

    private static String encodeResult(Object result, Class<?> declaredType) {
        if (declaredType == void.class || result == null) {
            return "N";
        }
        if (result instanceof Boolean) {
            return "Z:" + result.toString();
        }
        if (result instanceof Byte || result instanceof Short ||
                result instanceof Integer || result instanceof Long ||
                result instanceof Character) {
            return "I:" + result.toString();
        }
        if (result instanceof Float || result instanceof Double) {
            return "D:" + result.toString();
        }
        if (result instanceof String || result instanceof CharSequence) {
            return "S:" + result.toString();
        }
        synchronized (REFLECT_OBJECTS) {
            int id = nextReflectId++;
            REFLECT_OBJECTS.put(id, result);
            return "O:" + id + ":" + result.getClass().getName();
        }
    }

    private static String encodeError(Throwable error) {
        Throwable actual = error;
        if (actual instanceof java.lang.reflect.InvocationTargetException &&
                ((java.lang.reflect.InvocationTargetException) actual).getTargetException() != null) {
            actual = ((java.lang.reflect.InvocationTargetException) actual).getTargetException();
        }
        String message = actual.getClass().getSimpleName();
        if (actual.getMessage() != null && actual.getMessage().length() > 0) {
            message += ": " + actual.getMessage();
        }
        Log.e(TAG, "Reflection call failed.", actual);
        return "E:" + message;
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
        if (overlay != null && overlay.view != null) {
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
            String assetName = assetNameFromSource(source);
            if (assetName != null) {
                InputStream input = image.getContext().getAssets().open(assetName);
                try {
                    Bitmap bitmap = BitmapFactory.decodeStream(input);
                    image.setImageBitmap(bitmap);
                    return bitmap != null;
                } finally {
                    input.close();
                }
            }
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

    private static void loadWebView(WebView webView, String source) {
        String actual = source == null ? "" : source;
        String assetName = assetNameFromSource(actual);
        if (assetName != null) {
            webView.loadUrl(androidAssetUrl(assetName));
        } else if (isLoadableUrl(actual)) {
            webView.loadUrl(actual);
        } else {
            webView.loadDataWithBaseURL(null, actual, "text/html", "UTF-8", null);
        }
    }

    private static boolean isLoadableUrl(String source) {
        return source.startsWith("http://") ||
                source.startsWith("https://") ||
                source.startsWith("file://") ||
                source.startsWith("content://") ||
                source.startsWith("data:");
    }

    private static String assetNameFromSource(String source) {
        String value = source == null ? "" : source;
        if (value.startsWith("fpatch-asset://")) {
            return value.substring("fpatch-asset://".length());
        }
        if (value.startsWith("asset://")) {
            return value.substring("asset://".length());
        }
        if (value.startsWith("file:///android_asset/")) {
            return value.substring("file:///android_asset/".length());
        }
        return null;
    }

    private static String androidAssetUrl(String assetName) {
        StringBuilder url = new StringBuilder("file:///android_asset/");
        String[] parts = assetName.split("/");
        for (int i = 0; i < parts.length; i++) {
            if (i > 0) {
                url.append('/');
            }
            url.append(Uri.encode(parts[i]));
        }
        return url.toString();
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
        FrameLayout view;
        int width = ViewGroup.LayoutParams.MATCH_PARENT;
        int height = ViewGroup.LayoutParams.WRAP_CONTENT;
        int gravity = Gravity.TOP | Gravity.START;
        int x;
        int y;
        int backgroundColor = Color.TRANSPARENT;
        float alpha = 1f;
        boolean visible = true;

        OverlayState(int id) {
            this.id = id;
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
