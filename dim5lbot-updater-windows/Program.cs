using System.Diagnostics;
using System.IO.Compression;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text.Json.Serialization;

namespace Dim5lBotUpdater;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm());
    }
}

internal sealed class MainForm : Form
{
    private const string ManifestUrl = "https://raw.githubusercontent.com/dim5lbongE/dim5lBOT/main/updates/latest.json";
    private const string ModId = "lxdim5lxl.dim5lbot";
    private readonly HttpClient _http = new() { Timeout = TimeSpan.FromSeconds(45) };
    private readonly Label _status = new();
    private readonly Label _path = new();
    private readonly Label _version = new();
    private readonly ProgressBar _progress = new();
    private readonly Button _update = new();
    private readonly Button _browse = new();
    private readonly TextBox _log = new();
    private string? _modsDirectory;

    public MainForm()
    {
        Text = "dim5lBOT Updater";
        ClientSize = new Size(700, 620);
        MinimumSize = new Size(620, 560);
        StartPosition = FormStartPosition.CenterScreen;
        BackColor = Color.FromArgb(246, 248, 252);
        Font = new Font("Segoe UI", 10f);

        var header = new Label { Text = "dim5lBOT", Font = new Font("Segoe UI", 27, FontStyle.Bold), ForeColor = Color.FromArgb(25, 43, 76), AutoSize = true, Location = new Point(28, 22) };
        var subtitle = new Label { Text = "Windows Updater", Font = new Font("Segoe UI", 12), ForeColor = Color.FromArgb(210, 44, 54), AutoSize = true, Location = new Point(32, 72) };
        Controls.Add(header);
        Controls.Add(subtitle);

        var card = new Panel { BackColor = Color.White, Location = new Point(28, 108), Size = new Size(644, 190), Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right };
        _status.Text = "Geode 설치 폴더를 확인하는 중...";
        _status.Font = new Font("Segoe UI", 14, FontStyle.Bold);
        _status.ForeColor = Color.FromArgb(25, 43, 76);
        _status.AutoSize = true;
        _status.Location = new Point(20, 18);
        _version.AutoSize = true;
        _version.Location = new Point(20, 58);
        _path.AutoEllipsis = true;
        _path.Location = new Point(20, 88);
        _path.Size = new Size(600, 25);
        _path.ForeColor = Color.DimGray;
        _progress.Location = new Point(20, 122);
        _progress.Size = new Size(600, 12);
        _progress.Style = ProgressBarStyle.Continuous;
        _update.Text = "최신 버전 설치";
        _update.Location = new Point(20, 146);
        _update.Size = new Size(190, 34);
        _update.BackColor = Color.FromArgb(25, 43, 76);
        _update.ForeColor = Color.White;
        _update.FlatStyle = FlatStyle.Flat;
        _update.Enabled = false;
        _update.Click += async (_, _) => await InstallAsync();
        _browse.Text = "폴더 직접 선택";
        _browse.Location = new Point(220, 146);
        _browse.Size = new Size(150, 34);
        _browse.Click += (_, _) => Browse();
        card.Controls.AddRange([_status, _version, _path, _progress, _update, _browse]);
        Controls.Add(card);

        var changeTitle = new Label { Text = "업데이트 로그", Font = new Font("Segoe UI", 14, FontStyle.Bold), AutoSize = true, Location = new Point(28, 320), ForeColor = Color.FromArgb(25, 43, 76) };
        var changes = new Label { Text = "v1.1.0  LATEST\r\nmacOS 지원 · 기능 향상 · 버그 수정\r\n\r\nv1.0.0\r\ndim5lBOT 첫 정식 버전", AutoSize = true, Location = new Point(32, 358), ForeColor = Color.FromArgb(55, 60, 70) };
        Controls.Add(changeTitle);
        Controls.Add(changes);

        _log.Location = new Point(28, 470);
        _log.Size = new Size(644, 118);
        _log.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
        _log.Multiline = true;
        _log.ReadOnly = true;
        _log.ScrollBars = ScrollBars.Vertical;
        _log.BackColor = Color.FromArgb(237, 241, 248);
        _log.BorderStyle = BorderStyle.FixedSingle;
        Controls.Add(_log);
        Shown += async (_, _) => await InitializeAsync();
    }

    private async Task InitializeAsync()
    {
        _modsDirectory = FindModsDirectory();
        RenderDirectory();
        if (_modsDirectory is null) return;
        await RefreshVersionAsync();
    }

    private static string? FindModsDirectory()
    {
        var local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        var candidates = new[]
        {
            Path.Combine(local, "GeometryDash", "geode", "mods"),
            Path.Combine(local, "GeometryDash", "geode", "mods", "installed"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "GeometryDash", "geode", "mods")
        };
        return candidates.FirstOrDefault(Directory.Exists);
    }

    private void Browse()
    {
        using var picker = new FolderBrowserDialog { Description = "Geometry Dash의 geode\\mods 폴더를 선택하세요", UseDescriptionForTitle = true };
        if (picker.ShowDialog(this) != DialogResult.OK) return;
        _modsDirectory = picker.SelectedPath;
        RenderDirectory();
        _ = RefreshVersionAsync();
    }

    private void RenderDirectory()
    {
        if (_modsDirectory is null)
        {
            _status.Text = "Geode mods 폴더를 찾지 못했습니다";
            _path.Text = "‘폴더 직접 선택’을 눌러 geode\\mods 폴더를 선택하세요.";
            _update.Enabled = false;
            return;
        }
        _status.Text = "업데이트 준비 완료";
        _path.Text = _modsDirectory;
        _update.Enabled = true;
    }

    private async Task RefreshVersionAsync()
    {
        try
        {
            var installed = ReadInstalledVersion(_modsDirectory!);
            var latest = await _http.GetFromJsonAsync<UpdateManifest>(ManifestUrl) ?? throw new InvalidDataException("업데이트 정보가 비어 있습니다.");
            _version.Text = $"현재 버전: {installed ?? "미설치"}   /   최신 버전: v{latest.Version}";
            Log("업데이트 서버와 설치 폴더 확인 완료");
        }
        catch (Exception ex) { Fail("버전 확인 실패", ex); }
    }

    private async Task InstallAsync()
    {
        if (_modsDirectory is null) return;
        SetBusy(true);
        string? temp = null;
        string? backup = null;
        try
        {
            Log("최신 버전 정보 확인 중...");
            var manifest = await _http.GetFromJsonAsync<UpdateManifest>(ManifestUrl) ?? throw new InvalidDataException("latest.json을 읽지 못했습니다.");
            if (string.IsNullOrWhiteSpace(manifest.Url) || string.IsNullOrWhiteSpace(manifest.Sha256)) throw new InvalidDataException("업데이트 정보 형식이 잘못되었습니다.");

            temp = Path.Combine(_modsDirectory, $".{ModId}.{Guid.NewGuid():N}.download");
            Log($"v{manifest.Version} 다운로드 중...");
            await using (var input = await _http.GetStreamAsync(manifest.Url))
            await using (var output = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                await input.CopyToAsync(output);
            _progress.Value = 50;

            var actualHash = await Sha256Async(temp);
            if (!actualHash.Equals(manifest.Sha256, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("다운로드 파일의 SHA-256이 서버 정보와 다릅니다.");
            var package = ReadPackageInfo(temp);
            if (!package.Id.Equals(ModId, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("다운로드 파일의 모드 ID가 dim5lBOT이 아닙니다.");
            if (!NormalizeVersion(package.Version).Equals(NormalizeVersion(manifest.Version), StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("다운로드 파일 내부 버전이 최신 버전 정보와 다릅니다.");
            if (!package.SupportsWindows) throw new InvalidDataException("다운로드 파일에 Windows 지원이 표시되어 있지 않습니다.");
            _progress.Value = 75;

            var destination = Path.Combine(_modsDirectory, manifest.FileName ?? $"{ModId}.geode");
            if (File.Exists(destination))
            {
                backup = destination + ".backup";
                File.Copy(destination, backup, true);
                File.Replace(temp, destination, null);
                temp = null;
            }
            else
            {
                File.Move(temp, destination);
                temp = null;
            }

            var finalHash = await Sha256Async(destination);
            if (!finalHash.Equals(manifest.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                if (backup is not null) File.Copy(backup, destination, true);
                throw new IOException("설치 후 파일 검증에 실패하여 이전 버전을 복구했습니다.");
            }
            if (backup is not null && File.Exists(backup)) File.Delete(backup);
            _progress.Value = 100;
            _status.Text = $"v{manifest.Version} 업데이트 완료";
            _version.Text = $"현재 버전: v{manifest.Version}   /   최신 버전: v{manifest.Version}";
            Log($"설치 완료: {destination}");
            MessageBox.Show(this, "업데이트가 완료되었습니다. Geometry Dash를 다시 실행하세요.", "dim5lBOT", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (Exception ex) { Fail("업데이트 실패", ex); }
        finally
        {
            if (temp is not null && File.Exists(temp)) File.Delete(temp);
            SetBusy(false);
        }
    }

    private static string? ReadInstalledVersion(string directory)
    {
        foreach (var file in Directory.EnumerateFiles(directory, "*.geode", SearchOption.TopDirectoryOnly))
        {
            try { var info = ReadPackageInfo(file); if (info.Id.Equals(ModId, StringComparison.OrdinalIgnoreCase)) return info.Version; }
            catch { }
        }
        return null;
    }

    private static PackageInfo ReadPackageInfo(string file)
    {
        using var archive = ZipFile.OpenRead(file);
        var entry = archive.GetEntry("mod.json") ?? throw new InvalidDataException("mod.json이 없는 잘못된 .geode 파일입니다.");
        using var stream = entry.Open();
        using var json = System.Text.Json.JsonDocument.Parse(stream);
        var root = json.RootElement;
        var id = root.GetProperty("id").GetString() ?? "";
        var version = root.GetProperty("version").GetString() ?? "";
        var supportsWindows = root.TryGetProperty("gd", out var gd) && gd.TryGetProperty("win", out _);
        return new PackageInfo(id, version, supportsWindows);
    }

    private static async Task<string> Sha256Async(string path)
    {
        await using var stream = File.OpenRead(path);
        return Convert.ToHexString(await SHA256.HashDataAsync(stream)).ToLowerInvariant();
    }

    private static string NormalizeVersion(string version) => version.Trim().TrimStart('v', 'V');
    private void SetBusy(bool busy) { _update.Enabled = !busy && _modsDirectory is not null; _browse.Enabled = !busy; if (!busy && _progress.Value != 100) _progress.Value = 0; }
    private void Log(string message) => _log.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}");
    private void Fail(string title, Exception ex) { _status.Text = title; Log($"{title}: {ex.Message}"); MessageBox.Show(this, ex.Message, title, MessageBoxButtons.OK, MessageBoxIcon.Error); }

    private sealed record PackageInfo(string Id, string Version, bool SupportsWindows);
    private sealed class UpdateManifest
    {
        [JsonPropertyName("version")] public string Version { get; set; } = "";
        [JsonPropertyName("fileName")] public string? FileName { get; set; }
        [JsonPropertyName("url")] public string Url { get; set; } = "";
        [JsonPropertyName("sha256")] public string Sha256 { get; set; } = "";
    }
}
