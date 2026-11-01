package org.metroidprime2.port;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.media.MediaScannerConnection;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Process;
import android.provider.Settings;
import android.Manifest;
import android.content.pm.PackageManager;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.util.Log;

import dev.encounter.aurora.AuroraSurface;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * The app: SDL's activity with this port's library, Aurora's surface, the port's directories
 * written down before the native entry runs, and - from milestone M3 - the on-screen
 * controller drawn over the game.
 *
 * <p>What is deliberately absent: the port's F1 settings window has no touch affordance, so
 * the layout editor is reached from the overlay's own edit pill (see TouchControlsView).
 */
public final class MetroidPrime2Activity extends SDLActivity {
    private static final String TAG = "MetroidPrime2Port";

    // The overlay, added to SDL's own layout so it sits over the surface. Null if SDL did not
    // build a layout, which only happens if library loading failed.
    private TouchControlsView touchControls;

    // Implemented in platform/port_memory_log.cpp; level -1 is onLowMemory.
    private static native void nativeMemoryWarning(int level, long availableKb, long thresholdKb, boolean low);
    // Implemented in platform/port_touch_jni.cpp.
    private static native void nativeDrawInsets(int left, int top, int right, int bottom);

    // How the previous run ended, for the log (MP1 issue #102): a low-memory kill leaves
    // no crash report, so the system's exit record is the only trace. Read by
    // port_memory_log.cpp once the log runs.
    private static volatile String exitReport = "";

    static String exitReport() {
        return exitReport;
    }

    // ActivityManager.isLowRamDevice() (Android Go and the like), read by
    // port_memory_log.cpp (platform/main.cpp builds pipelines on first use on such a device).
    private static volatile boolean lowRamDevice = false;

    static boolean lowRamDevice() {
        return lowRamDevice;
    }

    private String buildExitReport() {
        StringBuilder report = new StringBuilder();
        ActivityManager manager = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        if (manager == null) {
            return "";
        }
        ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
        manager.getMemoryInfo(memory);
        lowRamDevice = manager.isLowRamDevice();
        report.append(String.format(Locale.ROOT,
            "port: device memory: %d MiB, %d MiB available, low-memory threshold %d MiB, app heap class %d MiB",
            memory.totalMem >> 20, memory.availMem >> 20, memory.threshold >> 20, manager.getMemoryClass()));
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            report.append("\nport: previous run: exit reasons need Android 11");
            return report.toString();
        }
        ApplicationExitInfo last = null;
        for (ApplicationExitInfo info : manager.getHistoricalProcessExitReasons(getPackageName(), 0, 8)) {
            if (getPackageName().equals(info.getProcessName())) {
                last = info;
                break;
            }
        }
        if (last == null) {
            report.append("\nport: previous run: no exit record");
            return report.toString();
        }
        report.append(String.format(Locale.ROOT,
            "\nport: previous run: ended %s, %s (status %d, importance %d), memory pss %d MiB rss %d MiB",
            new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT).format(new java.util.Date(last.getTimestamp())),
            exitReasonName(last.getReason()), last.getStatus(), last.getImportance(),
            last.getPss() >> 10, last.getRss() >> 10));
        if (last.getDescription() != null) {
            report.append("\nport: previous run: ").append(last.getDescription());
        }
        // An ANR's trace is text (a native crash's is a protobuf tombstone): its head
        // says where the main thread was stuck.
        if (last.getReason() == ApplicationExitInfo.REASON_ANR) {
            try (InputStream trace = last.getTraceInputStream()) {
                if (trace != null) {
                    BufferedReader reader = new BufferedReader(new java.io.InputStreamReader(trace));
                    String line;
                    for (int i = 0; i < 60 && (line = reader.readLine()) != null; ++i) {
                        report.append("\n  ").append(line);
                    }
                }
            } catch (IOException e) {
                Log.w(TAG, "Could not read the ANR trace", e);
            }
        }
        return report.toString();
    }

    @android.annotation.TargetApi(Build.VERSION_CODES.R)
    private static String exitReasonName(int reason) {
        switch (reason) {
            case ApplicationExitInfo.REASON_EXIT_SELF: return "exited by itself";
            case ApplicationExitInfo.REASON_SIGNALED: return "killed by a signal";
            case ApplicationExitInfo.REASON_LOW_MEMORY: return "killed for low memory";
            case ApplicationExitInfo.REASON_CRASH: return "Java crash";
            case ApplicationExitInfo.REASON_CRASH_NATIVE: return "native crash";
            case ApplicationExitInfo.REASON_ANR: return "not responding (ANR)";
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE: return "initialization failure";
            case ApplicationExitInfo.REASON_PERMISSION_CHANGE: return "permission change";
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE: return "excessive resource usage";
            case ApplicationExitInfo.REASON_USER_REQUESTED: return "user requested (force stop)";
            case ApplicationExitInfo.REASON_USER_STOPPED: return "user stopped";
            case ApplicationExitInfo.REASON_DEPENDENCY_DIED: return "dependency died";
            case ApplicationExitInfo.REASON_OTHER: return "other (system)";
            case 14: return "freezer"; // REASON_FREEZER, API 33
            case 15: return "package state change"; // API 34
            case 16: return "package updated"; // API 34
            default: return "unknown reason " + reason;
        }
    }

    private void reportMemoryWarning(int level) {
        ActivityManager manager = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
        if (manager != null) {
            manager.getMemoryInfo(memory);
        }
        try {
            nativeMemoryWarning(level, memory.availMem >> 10, memory.threshold >> 10, memory.lowMemory);
        } catch (UnsatisfiedLinkError e) {
            // The library failed to load: no log to write to.
        }
    }

    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        reportMemoryWarning(level);
    }

    @Override
    public void onLowMemory() {
        super.onLowMemory();
        reportMemoryWarning(-1);
    }

    // The library the native build produces: CMakeLists.txt's `metroid_prime2_port`, which on
    // Android is libmetroid_prime2_port.so and exports SDL_main.
    @Override
    protected String[] getLibraries() {
        return new String[]{"metroid_prime2_port"};
    }

    // Aurora presents through its own surface, which is where the WebGPU swapchain attaches;
    // SDL's plain SDLSurface has nowhere to put it.
    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new AuroraSurface(context);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Before super.onCreate: that is where SDLActivity loads the library, and the native
        // entry reads this file as one of its first acts (platform/main.cpp,
        // ResolveAndroidPaths).
        writePortPaths();
        try {
            // Cleared first: copying only adds, so an icon a newer APK no longer ships
            // would keep replacing a texture it no longer claims. The native side reads
            // <files>/textures (platform/main.cpp, PortPaths::base) whatever the user
            // folder is moved to.
            File textures = new File(getFilesDir(), "textures");
            deleteTree(textures);
            copyAssetTree("textures", textures);
        } catch (IOException e) {
            Log.e(TAG, "Failed to unpack the button glyphs", e);
        }
        try {
            exitReport = buildExitReport();
        } catch (RuntimeException e) {
            Log.w(TAG, "Could not read the previous exit", e);
        }
        super.onCreate(savedInstanceState);
        // SDLActivity.onCreate sets the window to non-fullscreen; the game wants the whole screen.
        setWindowStyle(true);
        addTouchControls();
        Log.i(TAG, "activity created; files dir " + getFilesDir());
    }

    /**
     * SDL keeps its surface in {@code mLayout}; adding the overlay to the same layout is what
     * MP1's activity does, and it is the only way to draw over the game without touching SDL.
     * The view covers the whole layout and consumes the touches it does not pass on, so SDL
     * never sees them - which is why the native side feeds the game a virtual pad from the JNI
     * callbacks instead of trying to synthesise input events.
     */
    private void addTouchControls() {
        if (mLayout == null) {
            Log.w(TAG, "SDL has no layout; no touch controls");
            return;
        }
        touchControls = new TouchControlsView(this);
        mLayout.addView(touchControls, new android.widget.RelativeLayout.LayoutParams(
            android.widget.RelativeLayout.LayoutParams.MATCH_PARENT,
            android.widget.RelativeLayout.LayoutParams.MATCH_PARENT));
        // SDL's safe area includes the gesture zones, which stay reported with the bars hidden (a
        // ~140 px band along the top on ColorOS). The settings window's top edge only needs to
        // avoid the cutout and a bar on screen (window.cpp DrawArea); the insets still reach
        // SDLSurface and the overlay below.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            mLayout.setOnApplyWindowInsetsListener((view, insets) -> {
                android.graphics.Insets draw = insets.getInsets(
                    android.view.WindowInsets.Type.systemBars() |
                    android.view.WindowInsets.Type.displayCutout());
                nativeDrawInsets(draw.left, draw.top, draw.right, draw.bottom);
                return insets;
            });
        }
    }

    /**
     * The native side resolves the port's directories beneath SDL's preference directory -
     * which on Android is this same app-private files directory - and reads user=, cache= and
     * disc= overrides from a port_paths.txt beside them. Writing the file here makes the layout
     * visible in the log from the first launch and leaves the place the file picker will put
     * the disc it copies in the next milestone. An existing file is left alone, so a pushed
     * port_paths.txt still wins, which is what a debugging session wants.
     */
    private void writePortPaths() {
        File marker = new File(getFilesDir(), "port_paths.txt");
        if (marker.exists()) {
            return;
        }
        String listed = "user=" + new File(getFilesDir(), "user") + "\n"
            + "cache=" + new File(getFilesDir(), "cache") + "\n"
            + "disc=" + new File(getFilesDir(), "disc.iso") + "\n";
        try (FileOutputStream output = new FileOutputStream(marker)) {
            output.write(listed.getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            // Not fatal: the native side falls back to those same directories.
            Log.w(TAG, "Could not write " + marker, e);
        }
    }

    private static final int REQUEST_STORAGE = 0x57a6;

    // Whether native code may read and write shared storage with plain file calls
    // (platform/port_data_folder.cpp).
    public boolean hasStorageAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }
        return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
            == PackageManager.PERMISSION_GRANTED;
    }

    // Opens the system's "All files access" page (Android 11+) or asks for the storage
    // permission (9-10).
    public void requestStorageAccess() {
        runOnUiThread(() -> {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
                requestPermissions(new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQUEST_STORAGE);
                return;
            }
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName())));
            } catch (android.content.ActivityNotFoundException e) {
                try {
                    startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                } catch (android.content.ActivityNotFoundException e2) {
                    Log.w(TAG, "No settings page for all files access", e2);
                }
            }
        });
    }

    // Starts the game again in a new process (RestartActivity), so the data folder chosen in the
    // overlay is picked up.
    public void restartApp() {
        runOnUiThread(() -> {
            Intent intent = new Intent(this, RestartActivity.class);
            intent.putExtra(RestartActivity.EXTRA_PID, Process.myPid());
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(intent);
        });
    }

    private static final int REQUEST_TEXTURE_PACK = 0x7e57;
    // Ask the system picker to list the phone's storage root alongside its shortcuts.
    private static final String SHOW_ADVANCED = "android.content.extra.SHOW_ADVANCED";
    // A disc picked after Android destroyed the activity behind the picker reaches a new
    // launch, which drops it: SDL's dialog state died with the old process. The picker's
    // request code is kept here, and such a pick is left in PENDING_DISC_FILE for the
    // native side's next start (platform/main.cpp).
    private static final String DISC_PICK_PREFS = "disc_pick";
    private static final String PENDING_DISC_FILE = "pending_disc_uri";

    // Texture pack picker (platform/debug_ui/game_sections.cpp, nativeTexturePack* in
    // platform/port_touch_jni.cpp): the folder is copied to <user>/user_textures.new, which
    // PortTextures swaps in on its next frame.
    private static native void nativeTexturePackStatus(String status);
    private static native void nativeTexturePackReady();

    private final AtomicBoolean texturePackCopying = new AtomicBoolean();
    private volatile String texturePackFolder;

    // Called from the debug overlay, on the SDL thread, with the user folder the pack goes into.
    public void pickTexturePack(String folder) {
        runOnUiThread(() -> {
            if (texturePackCopying.get()) {
                nativeTexturePackStatus("A texture pack is still being copied.");
                return;
            }
            texturePackFolder = folder;
            try {
                startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
                                           .putExtra(SHOW_ADVANCED, true),
                                       REQUEST_TEXTURE_PACK);
            } catch (android.content.ActivityNotFoundException e) {
                nativeTexturePackStatus("This device has no folder picker.");
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQUEST_TEXTURE_PACK) {
            Uri tree = data != null && resultCode == RESULT_OK ? data.getData() : null;
            if (tree != null) {
                copyTexturePack(tree);
            }
            return;
        }
        android.content.SharedPreferences discPick = getSharedPreferences(DISC_PICK_PREFS, MODE_PRIVATE);
        if (discPick.getInt("request", Integer.MIN_VALUE) == requestCode) {
            discPick.edit().remove("request").commit();
            Uri disc = data != null && resultCode == RESULT_OK ? data.getData() : null;
            if (disc != null && !isFileDialogPending(requestCode)) {
                try (OutputStream out = new FileOutputStream(new File(getFilesDir(), PENDING_DISC_FILE))) {
                    out.write(disc.toString().getBytes(StandardCharsets.UTF_8));
                    Log.i(TAG, "Kept a disc picked before the activity was recreated: " + disc);
                } catch (IOException e) {
                    Log.w(TAG, "Could not keep the disc picked before the activity was recreated", e);
                }
                return;
            }
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    // Remembers which dialog is the disc picker (opening, with filters; the texture pack
    // uses its own intent) in storage that outlives the process. commit, not apply: the
    // process can be gone before an apply is written.
    @Override
    protected void onFileDialogShown(String[] filters, boolean forWrite, int requestCode) {
        android.content.SharedPreferences.Editor edit = getSharedPreferences(DISC_PICK_PREFS, MODE_PRIVATE).edit();
        if (!forWrite && filters != null && filters.length > 0) {
            edit.putInt("request", requestCode);
        } else {
            edit.remove("request");
        }
        edit.commit();
    }

    // A copy rather than reading through the URI: the grant can be revoked, and the loader needs
    // real paths. The built-in set is never touched.
    private void copyTexturePack(Uri tree) {
        if (!texturePackCopying.compareAndSet(false, true)) {
            return;
        }
        nativeTexturePackStatus("Copying the texture pack...");
        new Thread(() -> {
            String folder = texturePackFolder;
            File root = folder != null && !folder.isEmpty() ? new File(folder) : getFilesDir();
            File staging = new File(root, "user_textures.partial");
            File ready = new File(root, "user_textures.new");
            try {
                deleteTree(staging);
                int[] copied = {0};
                String rootId = DocumentsContract.getTreeDocumentId(tree);
                copyDocumentTree(tree, rootId, staging, copied);
                if (copied[0] == 0) {
                    deleteTree(staging);
                    nativeTexturePackStatus("No .png or .dds textures in that folder; nothing changed.");
                    return;
                }
                deleteTree(ready);
                if (!staging.renameTo(ready)) {
                    throw new IOException("Failed to rename " + staging + " to " + ready);
                }
                nativeTexturePackStatus(String.format(Locale.ROOT, "Copied %d textures.", copied[0]));
                nativeTexturePackReady();
            } catch (IOException | RuntimeException e) {
                Log.e(TAG, "Failed to copy the texture pack", e);
                deleteTree(staging);
                nativeTexturePackStatus("Copying the texture pack failed: " + e.getMessage());
            } finally {
                texturePackCopying.set(false);
            }
        }, "texture-pack-copy").start();
    }

    private void copyDocumentTree(Uri tree, String parentId, File destination, int[] copied)
            throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        String[] columns = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
        };
        try (Cursor cursor = getContentResolver().query(children, columns, null, null, null)) {
            if (cursor == null) {
                throw new IOException("Cannot list " + children);
            }
            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                // A name from the provider becomes a path component here.
                if (name == null || name.isEmpty() || name.equals(".") || name.equals("..")
                        || name.contains("/")) {
                    continue;
                }
                File target = new File(destination, name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    copyDocumentTree(tree, id, target, copied);
                    continue;
                }
                String lower = name.toLowerCase(Locale.ROOT);
                if (!lower.endsWith(".png") && !lower.endsWith(".dds")) {
                    continue;
                }
                if (!destination.isDirectory() && !destination.mkdirs()) {
                    throw new IOException("Failed to create " + destination);
                }
                Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                try (InputStream input = getContentResolver().openInputStream(document);
                     FileOutputStream output = new FileOutputStream(target)) {
                    if (input == null) {
                        throw new IOException("Cannot open " + document);
                    }
                    byte[] buffer = new byte[64 * 1024];
                    int count;
                    while ((count = input.read(buffer)) != -1) {
                        output.write(buffer, 0, count);
                    }
                }
                if (++copied[0] % 100 == 0) {
                    nativeTexturePackStatus(String.format(Locale.ROOT,
                        "Copying the texture pack... %d textures", copied[0]));
                }
            }
        }
    }

    private void copyAssetTree(String assetPath, File destination) throws IOException {
        String[] children = getAssets().list(assetPath);
        if (children == null || children.length == 0) {
            File parent = destination.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                throw new IOException("Failed to create " + parent);
            }
            try (InputStream input = getAssets().open(assetPath);
                 FileOutputStream output = new FileOutputStream(destination)) {
                byte[] buffer = new byte[64 * 1024];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    output.write(buffer, 0, count);
                }
            }
            return;
        }
        if (!destination.isDirectory() && !destination.mkdirs()) {
            throw new IOException("Failed to create " + destination);
        }
        for (String child : children) {
            copyAssetTree(assetPath + "/" + child, new File(destination, child));
        }
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        file.delete();
    }

    // Called from photo mode (port_photo.cpp), on the SDL thread, once a photo is
    // written: moves it into the shared Pictures/Metroid Prime 2 folder, where the
    // gallery sees it. Returns where it went, or null (the file is then kept).
    // (MP1 MetroidPrimeActivity.exportPhoto.)
    public String exportPhoto(String path) {
        File source = new File(path);
        String folder = Environment.DIRECTORY_PICTURES + "/Metroid Prime 2";
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                ContentResolver resolver = getContentResolver();
                ContentValues values = new ContentValues();
                values.put(MediaStore.Images.Media.DISPLAY_NAME, source.getName());
                values.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
                values.put(MediaStore.Images.Media.RELATIVE_PATH, folder);
                values.put(MediaStore.Images.Media.IS_PENDING, 1);
                Uri uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
                if (uri == null) {
                    return null;
                }
                try (InputStream input = new FileInputStream(source);
                     OutputStream output = resolver.openOutputStream(uri)) {
                    if (output == null) {
                        throw new IOException("No stream for " + uri);
                    }
                    copyStream(input, output);
                } catch (IOException e) {
                    resolver.delete(uri, null, null);
                    throw e;
                }
                values.clear();
                values.put(MediaStore.Images.Media.IS_PENDING, 0);
                resolver.update(uri, values, null, null);
            } else {
                // Android 9 has no MediaStore folders: a plain file (WRITE_EXTERNAL_STORAGE,
                // maxSdkVersion 28), then a scan for the gallery.
                File dir = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_PICTURES),
                                    "Metroid Prime 2");
                if (!dir.isDirectory() && !dir.mkdirs()) {
                    throw new IOException("Failed to create " + dir);
                }
                File destination = new File(dir, source.getName());
                try (InputStream input = new FileInputStream(source);
                     OutputStream output = new FileOutputStream(destination)) {
                    copyStream(input, output);
                }
                MediaScannerConnection.scanFile(this, new String[]{destination.getPath()},
                                                new String[]{"image/png"}, null);
            }
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "Could not move " + source + " to " + folder, e);
            return null;
        }
        source.delete();
        return folder + "/" + source.getName();
    }

    private static void copyStream(InputStream input, OutputStream output) throws IOException {
        byte[] buffer = new byte[64 * 1024];
        int count;
        while ((count = input.read(buffer)) != -1) {
            output.write(buffer, 0, count);
        }
    }

}
