package com.dim5l.botupdater;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
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
    private static final String EXPECTED_TREE_ID =
        "primary:Android/media/com.geode.launcher/game/geode/mods";

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
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(22), dp(34), dp(22), dp(30));
        root.setBackgroundColor(Color.rgb(11, 5, 38));

        ImageView logo = new ImageView(this);
        logo.setImageResource(com.dim5l.botupdater.R.drawable.dim5lbot_update_logo);
        logo.setScaleType(ImageView.ScaleType.FIT_CENTER);
        LinearLayout.LayoutParams logoParams = new LinearLayout.LayoutParams(-1, dp(154));
        logoParams.setMargins(0, 0, 0, dp(12));
        logo.setLayoutParams(logoParams);
        root.addView(logo);

        TextView title = text("dim5lBOT Updater", 29, Color.WHITE);
        title.setGravity(Gravity.CENTER);
        title.setTypeface(Typeface.create("sans-serif", Typeface.BOLD));
        root.addView(title);
        TextView subtitle = text("빠르고 안전한 자동 업데이트", 14, Color.rgb(193, 181, 238));
        subtitle.setGravity(Gravity.CENTER);
        subtitle.setPadding(0, dp(5), 0, dp(24));
        root.addView(subtitle);

        LinearLayout updateCard = card();
        TextView cardLabel = text("UPDATE STATUS", 11, Color.rgb(168, 148, 235));
        cardLabel.setTypeface(Typeface.DEFAULT_BOLD);
        updateCard.addView(cardLabel);
        folderText = text("", 14, Color.LTGRAY);
        folderText.setPadding(0, dp(12), 0, 0);
        updateCard.addView(folderText);

        versionText = text("버전 확인 전", 18, Color.WHITE);
        versionText.setTypeface(Typeface.DEFAULT_BOLD);
        versionText.setPadding(0, dp(14), 0, dp(7));
        updateCard.addView(versionText);
        statusText = text("업데이트 정보를 확인합니다.", 14, Color.rgb(180, 195, 230));
        statusText.setPadding(0, 0, 0, dp(14));
        updateCard.addView(statusText);

        updateButton = button("업데이트 확인", v -> {
            if (latest == null) checkForUpdates(false); else installLatest();
        });
        updateButton.setBackground(rounded(Color.rgb(225, 20, 52), 14));
        updateButton.setTextColor(Color.WHITE);
        updateButton.setTypeface(Typeface.DEFAULT_BOLD);
        updateCard.addView(updateButton);
        Button folderButton = button("Geode mods 폴더 연결", v -> chooseFolder());
        folderButton.setBackground(rounded(Color.rgb(52, 37, 103), 14));
        folderButton.setTextColor(Color.WHITE);
        updateCard.addView(folderButton);
        root.addView(updateCard);

        TextView guideTitle = text("처음 사용하는 방법", 19, Color.WHITE);
        guideTitle.setTypeface(Typeface.DEFAULT_BOLD);
        guideTitle.setPadding(dp(2), dp(28), 0, dp(12));
        root.addView(guideTitle);
        LinearLayout guide = card();
        guide.addView(step("1", "폴더 연결", "Geode mods 폴더를 처음 한 번만 선택하세요."));
        guide.addView(step("2", "업데이트 확인", "앱을 열면 최신 dim5lBOT 버전을 자동 확인합니다."));
        guide.addView(step("3", "Geode 재실행", "설치 완료 후 Geode Launcher를 다시 실행하세요."));
        root.addView(guide);

        TextView logTitle = text("업데이트 로그", 19, Color.WHITE);
        logTitle.setTypeface(Typeface.DEFAULT_BOLD);
        logTitle.setPadding(dp(2), dp(28), 0, dp(12));
        root.addView(logTitle);
        LinearLayout logCard = card();
        logCard.addView(changeLog(
            "v1.1.0", "LATEST",
            "• macOS 지원\n" +
            "• 기능 향상\n" +
            "• 버그 수정"
        ));
        View divider = new View(this);
        divider.setBackgroundColor(Color.rgb(62, 49, 104));
        LinearLayout.LayoutParams dividerParams = new LinearLayout.LayoutParams(-1, dp(1));
        dividerParams.setMargins(0, dp(15), 0, dp(15));
        divider.setLayoutParams(dividerParams);
        logCard.addView(divider);
        logCard.addView(changeLog(
            "v1.0.0", "RELEASE",
            "• dim5lBOT 첫 정식 버전"
        ));
        root.addView(logCard);

        TextView path = text("권장 폴더\nAndroid/media/com.geode.launcher/game/geode/mods", 12, Color.rgb(145, 137, 179));
        path.setGravity(Gravity.CENTER);
        path.setPadding(0, dp(22), 0, 0);
        root.addView(path);
        scroll.addView(root);
        setContentView(scroll);
    }

    private LinearLayout card() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(20), dp(20), dp(20), dp(20));
        card.setBackground(rounded(Color.rgb(27, 18, 63), 22));
        card.setElevation(dp(3));
        card.setLayoutParams(new LinearLayout.LayoutParams(-1, -2));
        return card;
    }

    private LinearLayout step(String number, String heading, String body) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.TOP);
        row.setPadding(0, dp(8), 0, dp(8));
        TextView badge = text(number, 14, Color.WHITE);
        badge.setGravity(Gravity.CENTER);
        badge.setTypeface(Typeface.DEFAULT_BOLD);
        badge.setBackground(rounded(Color.rgb(225, 20, 52), 12));
        badge.setLayoutParams(new LinearLayout.LayoutParams(dp(32), dp(32)));
        row.addView(badge);
        LinearLayout copy = new LinearLayout(this);
        copy.setOrientation(LinearLayout.VERTICAL);
        copy.setPadding(dp(14), 0, 0, 0);
        TextView h = text(heading, 15, Color.WHITE);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        copy.addView(h);
        TextView b = text(body, 13, Color.rgb(190, 185, 211));
        b.setPadding(0, dp(3), 0, 0);
        copy.addView(b);
        row.addView(copy, new LinearLayout.LayoutParams(0, -2, 1));
        return row;
    }

    private LinearLayout changeLog(String version, String badgeText, String changes) {
        LinearLayout block = new LinearLayout(this);
        block.setOrientation(LinearLayout.VERTICAL);
        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        TextView versionText = text(version, 17, Color.WHITE);
        versionText.setTypeface(Typeface.DEFAULT_BOLD);
        header.addView(versionText);
        TextView badge = text(badgeText, 10, Color.rgb(235, 223, 255));
        badge.setGravity(Gravity.CENTER);
        badge.setTypeface(Typeface.DEFAULT_BOLD);
        badge.setBackground(rounded(Color.rgb(75, 50, 139), 10));
        LinearLayout.LayoutParams badgeParams = new LinearLayout.LayoutParams(-2, dp(24));
        badgeParams.setMargins(dp(10), 0, 0, 0);
        badge.setPadding(dp(9), 0, dp(9), 0);
        badge.setLayoutParams(badgeParams);
        header.addView(badge);
        block.addView(header);
        TextView body = text(changes, 13, Color.rgb(198, 193, 218));
        body.setPadding(0, dp(9), 0, 0);
        body.setLineSpacing(dp(3), 1.18f);
        block.addView(body);
        return block;
    }

    private GradientDrawable rounded(int color, int radiusDp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(radiusDp));
        return drawable;
    }

    private TextView text(String value, int sp, int color) {
        TextView view = new TextView(this);
        view.setText(value); view.setTextSize(sp); view.setTextColor(color);
        view.setFontFeatureSettings("kern");
        view.setLineSpacing(0, 1.18f);
        return view;
    }

    private Button button(String value, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(value); button.setTextSize(15); button.setAllCaps(false); button.setOnClickListener(listener);
        button.setStateListAnimator(null);
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
        try {
            String selectedId = DocumentsContract.getTreeDocumentId(modsTree);
            if (!EXPECTED_TREE_ID.equals(selectedId)) {
                modsTree = null;
                fail("잘못된 폴더입니다. Android/media/com.geode.launcher/game/geode/mods를 선택하세요.\n선택값: " + selectedId);
                return;
            }
        } catch (Exception error) {
            modsTree = null;
            fail("선택한 폴더 경로를 확인할 수 없습니다.");
            return;
        }
        int flags = data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        try { getContentResolver().takePersistableUriPermission(modsTree, flags); }
        catch (SecurityException ignored) { }
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_TREE, modsTree.toString()).apply();
        refreshFolderText();
        checkForUpdates(false);
    }

    private void refreshFolderText() {
        if (modsTree == null) {
            folderText.setText("모드 폴더: 선택되지 않음");
            return;
        }
        try {
            folderText.setText("모드 폴더: " + DocumentsContract.getTreeDocumentId(modsTree));
        } catch (Exception error) {
            folderText.setText("모드 폴더: 경로 확인 실패");
        }
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
                requireCorrectModsFolder();
                byte[] bytes = downloadBytes(info.url);
                try (FileOutputStream out = new FileOutputStream(temp)) { out.write(bytes); }
                String actual = sha256(new FileInputStream(temp));
                if (!info.sha256.equals(actual)) throw new Exception("SHA-256 검증 실패");
                long installedBytes = replaceInTree(info.fileName, temp);
                runOnUiThread(() -> {
                    statusText.setText("업데이트 완료: " + info.fileName + " (" + installedBytes + " bytes)\n"
                        + EXPECTED_TREE_ID + "\nGeode Launcher를 완전히 종료한 뒤 다시 실행하세요.");
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
        requireCorrectModsFolder();
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

    private long replaceInTree(String fileName, File source) throws Exception {
        ContentResolver resolver = getContentResolver();
        requireCorrectModsFolder();
        Uri target = findChild(fileName);
        if (target == null) {
            String treeId = DocumentsContract.getTreeDocumentId(modsTree);
            Uri parent = DocumentsContract.buildDocumentUriUsingTree(modsTree, treeId);
            target = DocumentsContract.createDocument(resolver, parent, "application/octet-stream", fileName);
            if (target == null) throw new Exception("모드 파일을 만들 수 없습니다. mods 폴더를 다시 선택하세요.");
        }

        OutputStream targetStream;
        try {
            targetStream = resolver.openOutputStream(target, "rwt");
        } catch (java.io.FileNotFoundException unsupportedMode) {
            targetStream = resolver.openOutputStream(target, "w");
        }
        try (InputStream in = new FileInputStream(source); OutputStream out = targetStream) {
            if (out == null) throw new Exception("모드 폴더 쓰기 실패");
            byte[] buffer = new byte[16384]; int count;
            while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
            out.flush();
        }

        String expected = sha256(new FileInputStream(source));
        String written;
        try (InputStream in = resolver.openInputStream(target)) {
            if (in == null) throw new Exception("설치 파일을 다시 읽을 수 없습니다.");
            written = sha256(in);
        }
        if (!expected.equals(written)) throw new Exception("모드 파일 쓰기 검증 실패");
        Uri verifiedTarget = findChild(fileName);
        if (verifiedTarget == null) throw new Exception("저장 후 mods 폴더에서 파일을 찾을 수 없습니다.");
        long size = querySize(verifiedTarget);
        if (size != source.length()) throw new Exception("저장 크기 불일치: " + size + " / " + source.length());
        return size;
    }

    private void requireCorrectModsFolder() throws Exception {
        if (modsTree == null) throw new Exception("mods 폴더가 선택되지 않았습니다.");
        String selectedId = DocumentsContract.getTreeDocumentId(modsTree);
        if (!EXPECTED_TREE_ID.equals(selectedId))
            throw new Exception("잘못된 폴더: " + selectedId);
    }

    private long querySize(Uri file) throws Exception {
        try (Cursor c = getContentResolver().query(file,
            new String[]{DocumentsContract.Document.COLUMN_SIZE}, null, null, null)) {
            if (c == null || !c.moveToFirst() || c.isNull(0)) throw new Exception("설치 파일 크기를 확인할 수 없습니다.");
            return c.getLong(0);
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
