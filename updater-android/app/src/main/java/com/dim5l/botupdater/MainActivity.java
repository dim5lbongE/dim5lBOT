package com.dim5l.botupdater;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends Activity {
    private static final int PICK_MODS_FOLDER = 4101;
    private static final String PREFS = "updater";
    private static final String KEY_TREE = "mods_tree";
    private static final String MANIFEST_URL =
        "https://raw.githubusercontent.com/dim5lbongE/dim5lBOT/main/updates/latest.json";

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private TextView folderText;
    private TextView versionText;
    private TextView statusText;
    private Button updateButton;
    private Uri modsTree;
    private UpdateInfo latest;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        buildUi();
        String saved = getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_TREE, null);
        if (saved != null) modsTree = Uri.parse(saved);
        refreshFolderText();
        checkForUpdates(true);
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(44), dp(24), dp(24));
        root.setBackgroundColor(Color.rgb(23, 17, 44));

        TextView title = text("dim5lBOT", 30, Color.WHITE);
        title.setTypeface(null, 1);
        root.addView(title);
        TextView subtitle = text("AUTOMATIC UPDATER", 13, Color.rgb(211, 188, 255));
        subtitle.setPadding(0, 0, 0, dp(28));
        root.addView(subtitle);

        folderText = text("", 14, Color.LTGRAY);
        root.addView(folderText);
        root.addView(button("Geode mods 폴더 선택", v -> chooseFolder()));

        versionText = text("버전 확인 전", 18, Color.WHITE);
        versionText.setPadding(0, dp(28), 0, dp(10));
        root.addView(versionText);
        statusText = text("업데이트 정보를 확인합니다.", 14, Color.rgb(180, 195, 230));
        statusText.setPadding(0, 0, 0, dp(20));
        root.addView(statusText);

        updateButton = button("업데이트 확인", v -> {
            if (latest == null) checkForUpdates(false); else installLatest();
        });
        root.addView(updateButton);

        TextView hint = text(
            "처음 한 번만 Android/media/com.geode.launcher/game/geode/mods 폴더를 선택하세요. 이후 앱을 열면 자동으로 최신 버전을 확인합니다.",
            12, Color.rgb(145, 145, 175));
        hint.setPadding(0, dp(30), 0, 0);
        root.addView(hint);
        setContentView(root);
    }

    private TextView text(String value, int sp, int color) {
        TextView view = new TextView(this);
        view.setText(value); view.setTextSize(sp); view.setTextColor(color);
        view.setLineSpacing(0, 1.15f);
        return view;
    }

    private Button button(String value, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(value); button.setAllCaps(false); button.setOnClickListener(listener);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, dp(52));
        p.setMargins(0, dp(10), 0, 0); button.setLayoutParams(p);
        return button;
    }

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }

    private void chooseFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
            Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        if (android.os.Build.VERSION.SDK_INT >= 26) {
            intent.putExtra("android.provider.extra.INITIAL_URI", Uri.parse(
                "content://com.android.externalstorage.documents/document/primary%3AAndroid%2Fmedia%2Fcom.geode.launcher%2Fgame%2Fgeode%2Fmods"));
        }
        startActivityForResult(intent, PICK_MODS_FOLDER);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_MODS_FOLDER || result != RESULT_OK || data == null || data.getData() == null) return;
        modsTree = data.getData();
        int flags = data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        try { getContentResolver().takePersistableUriPermission(modsTree, flags); }
        catch (SecurityException ignored) { }
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_TREE, modsTree.toString()).apply();
        refreshFolderText();
        checkForUpdates(false);
    }

    private void refreshFolderText() {
        folderText.setText(modsTree == null ? "모드 폴더: 선택되지 않음" : "모드 폴더: 연결됨");
    }

    private void checkForUpdates(boolean automatic) {
        setBusy("최신 버전을 확인하는 중…");
        executor.execute(() -> {
            try {
                JSONObject json = new JSONObject(new String(downloadBytes(MANIFEST_URL), java.nio.charset.StandardCharsets.UTF_8));
                UpdateInfo info = new UpdateInfo(json.getString("version"), json.getString("url"),
                    json.getString("sha256").toLowerCase(Locale.ROOT), json.getString("fileName"));
                String installedHash = modsTree == null ? null : hashInstalled(info.fileName);
                runOnUiThread(() -> {
                    latest = info;
                    versionText.setText("최신 버전  " + info.version);
                    updateButton.setEnabled(true);
                    if (modsTree == null) {
                        statusText.setText("먼저 Geode mods 폴더를 선택하세요.");
                        updateButton.setText("폴더 선택 후 업데이트");
                    } else if (info.sha256.equals(installedHash)) {
                        statusText.setText("이미 최신 버전입니다.");
                        updateButton.setText("다시 확인");
                        latest = null;
                    } else {
                        statusText.setText(automatic ? "새 업데이트를 찾았습니다." : "업데이트할 수 있습니다.");
                        updateButton.setText(info.version + " 설치");
                    }
                });
            } catch (Exception error) {
                runOnUiThread(() -> fail("확인 실패: " + error.getMessage()));
            }
        });
    }

    private void installLatest() {
        if (modsTree == null) { chooseFolder(); return; }
        final UpdateInfo info = latest;
        if (info == null) { checkForUpdates(false); return; }
        setBusy("다운로드 및 검증 중…");
        executor.execute(() -> {
            File temp = new File(getCacheDir(), info.fileName + ".download");
            try {
                byte[] bytes = downloadBytes(info.url);
                try (FileOutputStream out = new FileOutputStream(temp)) { out.write(bytes); }
                String actual = sha256(new FileInputStream(temp));
                if (!info.sha256.equals(actual)) throw new Exception("SHA-256 검증 실패");
                replaceInTree(info.fileName, temp);
                runOnUiThread(() -> {
                    statusText.setText("업데이트 완료. Geode Launcher를 다시 실행하세요.");
                    updateButton.setText("업데이트 확인"); updateButton.setEnabled(true); latest = null;
                    Toast.makeText(this, "dim5lBOT " + info.version + " 업데이트 완료", Toast.LENGTH_LONG).show();
                });
            } catch (Exception error) {
                runOnUiThread(() -> fail("설치 실패: " + error.getMessage()));
            } finally { temp.delete(); }
        });
    }

    private byte[] downloadBytes(String address) throws Exception {
        HttpURLConnection connection = (HttpURLConnection) new URL(address).openConnection();
        connection.setConnectTimeout(15000); connection.setReadTimeout(30000);
        connection.setRequestProperty("User-Agent", "dim5lBOT-Updater/1.0");
        if (connection.getResponseCode() / 100 != 2) throw new Exception("HTTP " + connection.getResponseCode());
        try (InputStream in = connection.getInputStream(); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[16384]; int count;
            while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
            return out.toByteArray();
        } finally { connection.disconnect(); }
    }

    private String hashInstalled(String fileName) throws Exception {
        Uri child = findChild(fileName);
        if (child == null) return null;
        try (InputStream in = getContentResolver().openInputStream(child)) { return sha256(in); }
    }

    private Uri findChild(String fileName) throws Exception {
        String treeId = DocumentsContract.getTreeDocumentId(modsTree);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(modsTree, treeId);
        try (Cursor c = getContentResolver().query(children,
            new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME},
            null, null, null)) {
            if (c == null) return null;
            while (c.moveToNext()) if (fileName.equals(c.getString(1)))
                return DocumentsContract.buildDocumentUriUsingTree(modsTree, c.getString(0));
        }
        return null;
    }

    private void replaceInTree(String fileName, File source) throws Exception {
        ContentResolver resolver = getContentResolver();
        Uri old = findChild(fileName);
        if (old != null && !DocumentsContract.deleteDocument(resolver, old)) throw new Exception("기존 파일 삭제 실패");
        Uri created = DocumentsContract.createDocument(resolver, modsTree, "application/octet-stream", fileName);
        if (created == null) throw new Exception("새 파일 생성 실패");
        try (InputStream in = new FileInputStream(source); OutputStream out = resolver.openOutputStream(created, "w")) {
            if (out == null) throw new Exception("모드 폴더 쓰기 실패");
            byte[] buffer = new byte[16384]; int count;
            while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
        }
    }

    private String sha256(InputStream in) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream source = in) {
            byte[] buffer = new byte[16384]; int count;
            while ((count = source.read(buffer)) != -1) digest.update(buffer, 0, count);
        }
        StringBuilder value = new StringBuilder();
        for (byte b : digest.digest()) value.append(String.format(Locale.ROOT, "%02x", b));
        return value.toString();
    }

    private void setBusy(String message) {
        statusText.setText(message); updateButton.setEnabled(false);
    }

    private void fail(String message) {
        statusText.setText(message); updateButton.setText("다시 시도"); updateButton.setEnabled(true); latest = null;
    }

    @Override protected void onDestroy() { super.onDestroy(); executor.shutdownNow(); }

    private static final class UpdateInfo {
        final String version, url, sha256, fileName;
        UpdateInfo(String version, String url, String sha256, String fileName) {
            this.version = version; this.url = url; this.sha256 = sha256; this.fileName = fileName;
        }
    }
}
