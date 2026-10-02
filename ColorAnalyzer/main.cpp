#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <windows.h>
#include <commdlg.h>

#define CVUI_IMPLEMENTATION
#include "cvui.h"

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")

using namespace cv;
using namespace std;

#define WINDOW_NAME "Color Analyzer"

// ============================================================
// БУФЕР ОБМЕНА
// ============================================================

void copyToClipboard(const string& text) {
    if (!OpenClipboard(NULL)) return;
    EmptyClipboard();
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (hMem) {
        memcpy(GlobalLock(hMem), text.c_str(), text.size() + 1);
        GlobalUnlock(hMem);
        SetClipboardData(CF_TEXT, hMem);
    }
    CloseClipboard();
}

// ============================================================
// БАЗОВЫЕ ФУНКЦИИ
// ============================================================

Mat toGray(const Mat& src) {
    Mat gray(src.rows, src.cols, CV_8UC1);
    for (int i = 0; i < src.rows; i++)
        for (int j = 0; j < src.cols; j++) {
            Vec3b p = src.at<Vec3b>(i, j);
            gray.at<uchar>(i, j) = (uchar)(0.299 * p[2] + 0.587 * p[1] + 0.114 * p[0]);
        }
    return gray;
}

double getHue(Vec3b p) {
    double b = p[0] / 255.0, g = p[1] / 255.0, r = p[2] / 255.0;
    double maxV = max(max(r, g), b);
    double minV = min(min(r, g), b);
    double delta = maxV - minV;
    double h = 0;
    if (delta != 0) {
        if (maxV == r) h = 60 * fmod((g - b) / delta, 6);
        else if (maxV == g) h = 60 * ((b - r) / delta + 2);
        else h = 60 * ((r - g) / delta + 4);
    }
    if (h < 0) h += 360;
    return h;
}

Mat getSaturation(const Mat& src) {
    Mat result(src.rows, src.cols, CV_8UC1);
    for (int i = 0; i < src.rows; i++)
        for (int j = 0; j < src.cols; j++) {
            Vec3b p = src.at<Vec3b>(i, j);
            uchar maxV = max(max(p[0], p[1]), p[2]);
            uchar minV = min(min(p[0], p[1]), p[2]);
            result.at<uchar>(i, j) = (maxV == 0) ? 0 : (uchar)(255.0 * (maxV - minV) / maxV);
        }
    return result;
}

Mat getHueMap(const Mat& src) {
    Mat result(src.rows, src.cols, CV_8UC1);
    for (int i = 0; i < src.rows; i++)
        for (int j = 0; j < src.cols; j++)
            result.at<uchar>(i, j) = (uchar)(getHue(src.at<Vec3b>(i, j)) / 360.0 * 255);
    return result;
}

Mat getColorMap(const Mat& src) {
    Mat result(src.rows, src.cols, CV_8UC3);
    for (int i = 0; i < src.rows; i++)
        for (int j = 0; j < src.cols; j++) {
            double h = getHue(src.at<Vec3b>(i, j));
            Vec3b color;
            if (h < 30 || h >= 330) color = Vec3b(0, 0, 255);
            else if (h < 60) color = Vec3b(0, 165, 255);
            else if (h < 90) color = Vec3b(0, 255, 255);
            else if (h < 150) color = Vec3b(0, 255, 0);
            else if (h < 210) color = Vec3b(255, 255, 0);
            else if (h < 270) color = Vec3b(255, 0, 0);
            else color = Vec3b(255, 0, 255);
            result.at<Vec3b>(i, j) = color;
        }
    return result;
}

// ============================================================
// РЕДАКТИРОВАНИЕ
// ============================================================

Mat adjustBrightness(const Mat& src, int delta) {
    Mat r = src.clone();
    for (int i = 0; i < r.rows; i++)
        for (int j = 0; j < r.cols; j++) {
            Vec3b p = r.at<Vec3b>(i, j);
            p[0] = saturate_cast<uchar>(p[0] + delta);
            p[1] = saturate_cast<uchar>(p[1] + delta);
            p[2] = saturate_cast<uchar>(p[2] + delta);
            r.at<Vec3b>(i, j) = p;
        }
    return r;
}

Mat adjustSaturation(const Mat& src, double k) {
    Mat r = src.clone();
    for (int i = 0; i < r.rows; i++)
        for (int j = 0; j < r.cols; j++) {
            Vec3b p = r.at<Vec3b>(i, j);
            double gray = 0.299 * p[2] + 0.587 * p[1] + 0.114 * p[0];
            for (int c = 0; c < 3; c++)
                p[c] = saturate_cast<uchar>(gray + k * (p[c] - gray));
            r.at<Vec3b>(i, j) = p;
        }
    return r;
}

Mat adjustContrast(const Mat& src, double k) {
    Mat r = src.clone();
    for (int i = 0; i < r.rows; i++)
        for (int j = 0; j < r.cols; j++) {
            Vec3b p = r.at<Vec3b>(i, j);
            for (int c = 0; c < 3; c++)
                p[c] = saturate_cast<uchar>((p[c] - 128) * k + 128);
            r.at<Vec3b>(i, j) = p;
        }
    return r;
}

Mat adjustGamma(const Mat& src, double gamma) {
    Mat r = src.clone();
    uchar lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = saturate_cast<uchar>(255.0 * pow(i / 255.0, 1.0 / gamma));
    for (int i = 0; i < r.rows; i++)
        for (int j = 0; j < r.cols; j++) {
            Vec3b p = r.at<Vec3b>(i, j);
            p[0] = lut[p[0]]; p[1] = lut[p[1]]; p[2] = lut[p[2]];
            r.at<Vec3b>(i, j) = p;
        }
    return r;
}

// ============================================================
// МЕТОД 1: ЗАМЕНА ЦВЕТА
// ============================================================

Mat replaceColor(const Mat& src, double targetHue, double newHue) {
    Mat r = src.clone();
    double hRange = 20.0;
    for (int i = 0; i < r.rows; i++)
        for (int j = 0; j < r.cols; j++) {
            Vec3b p = r.at<Vec3b>(i, j);
            double h = getHue(p);
            double diff = fabs(h - targetHue);
            if (diff > 180) diff = 360 - diff;
            if (diff < hRange) {
                uchar maxV = max(max(p[0], p[1]), p[2]);
                uchar minV = min(min(p[0], p[1]), p[2]);
                double s = (maxV > 0) ? (255.0 * (maxV - minV) / maxV) : 0;
                double hh = newHue, ss = s / 255.0, vv = maxV / 255.0;
                double c = vv * ss;
                double x = c * (1 - fabs(fmod(hh / 60.0, 2) - 1));
                double m = vv - c;
                double r1, g1, b1;
                if (hh < 60) { r1 = c; g1 = x; b1 = 0; }
                else if (hh < 120) { r1 = x; g1 = c; b1 = 0; }
                else if (hh < 180) { r1 = 0; g1 = c; b1 = x; }
                else if (hh < 240) { r1 = 0; g1 = x; b1 = c; }
                else if (hh < 300) { r1 = x; g1 = 0; b1 = c; }
                else { r1 = c; g1 = 0; b1 = x; }
                r.at<Vec3b>(i, j) = Vec3b(
                    saturate_cast<uchar>((b1 + m) * 255),
                    saturate_cast<uchar>((g1 + m) * 255),
                    saturate_cast<uchar>((r1 + m) * 255));
            }
        }
    return r;
}

// ============================================================
// МЕТОД 2: СБАЛАНСИРОВАНИЕ ЦВЕТОВ (серый мир)
// ============================================================

Mat balanceColors(const Mat& src) {
    double sumB = 0, sumG = 0, sumR = 0;
    int count = src.rows * src.cols;

    for (int i = 0; i < src.rows; i++)
        for (int j = 0; j < src.cols; j++) {
            Vec3b p = src.at<Vec3b>(i, j);
            sumB += p[0]; sumG += p[1]; sumR += p[2];
        }

    double avgB = sumB / count, avgG = sumG / count, avgR = sumR / count;
    double avg = (avgB + avgG + avgR) / 3.0;

    double kB = (avgB > 1) ? avg / avgB : 1.0;
    double kG = (avgG > 1) ? avg / avgG : 1.0;
    double kR = (avgR > 1) ? avg / avgR : 1.0;

    Mat r = src.clone();
    for (int i = 0; i < r.rows; i++)
        for (int j = 0; j < r.cols; j++) {
            Vec3b p = r.at<Vec3b>(i, j);
            p[0] = saturate_cast<uchar>(p[0] * kB);
            p[1] = saturate_cast<uchar>(p[1] * kG);
            p[2] = saturate_cast<uchar>(p[2] * kR);
            r.at<Vec3b>(i, j) = p;
        }
    return r;
}

// ============================================================
// ДОМИНИРУЮЩИЕ ЦВЕТА
// ============================================================

struct ColorInfo {
    Vec3b color;
    int percent;
};

vector<ColorInfo> getDominantColors(const Mat& src, int N) {
    const int LEVELS = 16;
    const int STEP = 256 / LEVELS;
    int freq[16][16][16] = { 0 };
    long long sumB[16][16][16] = { 0 };
    long long sumG[16][16][16] = { 0 };
    long long sumR[16][16][16] = { 0 };

    for (int i = 0; i < src.rows; i++)
        for (int j = 0; j < src.cols; j++) {
            Vec3b p = src.at<Vec3b>(i, j);
            int bi = min(p[0] / STEP, LEVELS - 1);
            int gi = min(p[1] / STEP, LEVELS - 1);
            int ri = min(p[2] / STEP, LEVELS - 1);
            freq[bi][gi][ri]++;
            sumB[bi][gi][ri] += p[0];
            sumG[bi][gi][ri] += p[1];
            sumR[bi][gi][ri] += p[2];
        }

    struct Bin { int b, g, r, count; };
    vector<Bin> bins;
    for (int bi = 0; bi < LEVELS; bi++)
        for (int gi = 0; gi < LEVELS; gi++)
            for (int ri = 0; ri < LEVELS; ri++)
                if (freq[bi][gi][ri] > 0)
                    bins.push_back({ bi, gi, ri, freq[bi][gi][ri] });

    for (size_t i = 0; i < bins.size(); i++)
        for (size_t j = i + 1; j < bins.size(); j++)
            if (bins[j].count > bins[i].count) swap(bins[i], bins[j]);

    vector<ColorInfo> result;
    int total = src.rows * src.cols;
    for (int i = 0; i < min(N, (int)bins.size()); i++) {
        int bi = bins[i].b, gi = bins[i].g, ri = bins[i].r;
        int count = bins[i].count;
        Vec3b avg(
            (uchar)(sumB[bi][gi][ri] / count),
            (uchar)(sumG[bi][gi][ri] / count),
            (uchar)(sumR[bi][gi][ri] / count));
        result.push_back({ avg, (int)(100.0 * count / total) });
    }
    return result;
}

// ============================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// ============================================================

int brightness = 100;
int saturation = 100;
int contrast = 100;
int gammaVal = 100;
int targetHue = 0;
int newHue = 180;
int mode = 0;

bool applyReplace = false;
bool applyBalance = false;
bool splitView = false;        // режим "До/После"

Mat src;
Mat cachedDisplay;
vector<ColorInfo> cachedColors;
bool needRecalc = true;

// ============================================================
// ПРИМЕНЕНИЕ
// ============================================================

Mat applyAll(const Mat& input) {
    Mat r = input.clone();
    r = adjustBrightness(r, brightness - 100);
    r = adjustSaturation(r, saturation / 100.0);
    r = adjustContrast(r, contrast / 100.0);
    r = adjustGamma(r, gammaVal / 100.0);
    if (applyBalance) r = balanceColors(r);
    if (applyReplace) r = replaceColor(r, targetHue, newHue);
    return r;
}

Mat getModeImage() {
    Mat e = applyAll(src);
    switch (mode) {
    case 0: return e;
    case 1: return toGray(e);
    case 2: return getSaturation(e);
    case 3: return getHueMap(e);
    case 4: return getColorMap(e);
    default: return e;
    }
}

void updateCache() {
    cachedDisplay = getModeImage();
    needRecalc = false;
}

unsigned int bgr2hex(Vec3b c) {
    return ((unsigned int)c[2] << 16) | ((unsigned int)c[1] << 8) | c[0];
}

// ============================================================
// ВЫБОР ФАЙЛА (открытие / сохранение)
// ============================================================

string openFileDialog() {
    OPENFILENAMEA ofn;
    char fileName[MAX_PATH] = "";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Images\0*.jpg;*.jpeg;*.png;*.bmp\0All Files\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = "Choose image";
    if (GetOpenFileNameA(&ofn)) return string(fileName);
    return "";
}

string saveFileDialog() {
    OPENFILENAMEA ofn;
    char fileName[MAX_PATH] = "result.jpg";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "JPEG Image\0*.jpg\0PNG Image\0*.png\0BMP Image\0*.bmp\0All Files\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "jpg";
    ofn.lpstrTitle = "Save image as";
    if (GetSaveFileNameA(&ofn)) return string(fileName);
    return "";
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "Russian");

    string path = openFileDialog();
    if (path.empty()) return 0;

    src = imread(path, IMREAD_COLOR);
    if (src.empty()) {
        MessageBoxA(NULL, "Cannot load image!", "Error", MB_ICONERROR);
        return -1;
    }

    const int MAX_SRC = 800;
    if (src.cols > MAX_SRC || src.rows > MAX_SRC) {
        double k = min((double)MAX_SRC / src.cols, (double)MAX_SRC / src.rows);
        resize(src, src, Size(), k, k);
    }

    cachedColors = getDominantColors(src, 6);
    cout << "Loaded: " << src.cols << "x" << src.rows << endl;
    cout << "Colors cached: " << cachedColors.size() << endl;

    updateCache();

    cvui::init(WINDOW_NAME, 20);

    string lastCopied = "";

    const int PANEL_W = 300;
    const int IMG_X = 10;
    const int IMG_Y = 10;

    int imgW = 500;
    int imgH = 400;
    int inputW = imgW;
    int inputH = imgH;

    while (true) {
        int panelX = IMG_X + imgW + 20;
        int canvasW = panelX + PANEL_W + 10;
        int canvasH = max(imgH + IMG_Y + 20, 900);

        Mat canvas(canvasH, canvasW, CV_8UC3, Scalar(35, 35, 35));

        if (needRecalc) updateCache();

        // ============ ИЗОБРАЖЕНИЕ ============
        if (splitView) {
            // Режим "До/После"
            int halfW = imgW / 2;

            // Левая половина — оригинал
            Mat origView = src.clone();
            if (origView.cols != halfW || origView.rows != imgH) {
                resize(origView, origView, Size(halfW, imgH));
            }
            if (origView.channels() == 1) cvtColor(origView, origView, COLOR_GRAY2BGR);
            origView.copyTo(canvas(Rect(IMG_X, IMG_Y, halfW, imgH)));

            // Правая половина — результат
            Mat resultView = cachedDisplay.clone();
            if (resultView.cols != halfW || resultView.rows != imgH) {
                resize(resultView, resultView, Size(halfW, imgH));
            }
            if (resultView.channels() == 1) cvtColor(resultView, resultView, COLOR_GRAY2BGR);
            resultView.copyTo(canvas(Rect(IMG_X + halfW, IMG_Y, halfW, imgH)));

            // Подписи
            cvui::text(canvas, IMG_X + 5, IMG_Y + 20, "BEFORE", 0.5, 0xffff00);
            cvui::text(canvas, IMG_X + halfW + 5, IMG_Y + 20, "AFTER", 0.5, 0x00ff00);

            // Разделительная линия
            for (int y = IMG_Y; y < IMG_Y + imgH; y++) {
                if (IMG_X + halfW < canvas.cols)
                    canvas.at<Vec3b>(y, IMG_X + halfW) = Vec3b(255, 255, 255);
                if (IMG_X + halfW + 1 < canvas.cols)
                    canvas.at<Vec3b>(y, IMG_X + halfW + 1) = Vec3b(255, 255, 255);
            }
        }
        else {
            // Обычный режим
            Mat img = cachedDisplay.clone();
            if (img.cols != imgW || img.rows != imgH) {
                resize(img, img, Size(imgW, imgH));
            }
            if (img.channels() == 1) cvtColor(img, img, COLOR_GRAY2BGR);
            img.copyTo(canvas(Rect(IMG_X, IMG_Y, imgW, imgH)));
        }

        // Уголок для ресайза
        for (int i = 0; i < 15; i++) {
            for (int j = 0; j < 15 - i; j++) {
                int px = IMG_X + imgW - 15 + i;
                int py = IMG_Y + imgH - 15 + j;
                if (px < canvas.cols && py < canvas.rows)
                    canvas.at<Vec3b>(py, px) = Vec3b(255, 255, 255);
            }
        }

        // ============ ПРАВАЯ ПАНЕЛЬ ============
        int px = panelX;
        int py = 10;

        cvui::text(canvas, px, py, "COLOR ANALYZER", 0.5, 0x00bfff);
        py += 25;

        // ---- ДОМИНИРУЮЩИЕ ЦВЕТА ----
        cvui::text(canvas, px, py, "Dominant colors", 0.4, 0xffffff);
        py += 18;

        int col1X = px;
        int col2X = px + 145;
        int startY = py;

        for (size_t i = 0; i < cachedColors.size(); i++) {
            Vec3b c = cachedColors[i].color;
            int pct = cachedColors[i].percent;

            int cx = (i % 2 == 0) ? col1X : col2X;
            int cy = startY + (i / 2) * 42;

            cvui::rect(canvas, cx, cy, 28, 28, 0xffffff, bgr2hex(c));

            char rgbBuf[32];
            sprintf_s(rgbBuf, "%d,%d,%d", (int)c[2], (int)c[1], (int)c[0]);
            cvui::text(canvas, cx + 32, cy + 2, rgbBuf, 0.35, 0xffffff);

            char hexBuf[16];
            sprintf_s(hexBuf, "#%02X%02X%02X", (int)c[2], (int)c[1], (int)c[0]);
            cvui::text(canvas, cx + 32, cy + 14, hexBuf, 0.3, 0xaaaaaa);

            char pctBuf[16];
            sprintf_s(pctBuf, "%d%%", pct);
            cvui::text(canvas, cx + 32, cy + 26, pctBuf, 0.3, 0xffff00);

            if (cvui::iarea(cx, cy, 28, 28) == cvui::CLICK) {
                char buf[128];
                sprintf_s(buf, "RGB(%d, %d, %d) #%02X%02X%02X",
                    (int)c[2], (int)c[1], (int)c[0],
                    (int)c[2], (int)c[1], (int)c[0]);
                copyToClipboard(buf);
                lastCopied = buf;
            }
            if (cvui::iarea(cx + 32, cy, 100, 14) == cvui::CLICK) {
                char buf[64];
                sprintf_s(buf, "RGB(%d, %d, %d)", (int)c[2], (int)c[1], (int)c[0]);
                copyToClipboard(buf);
                lastCopied = buf;
            }
            if (cvui::iarea(cx + 32, cy + 12, 100, 14) == cvui::CLICK) {
                char buf[16];
                sprintf_s(buf, "#%02X%02X%02X", (int)c[2], (int)c[1], (int)c[0]);
                copyToClipboard(buf);
                lastCopied = buf;
            }
        }

        py = startY + ((cachedColors.size() + 1) / 2) * 42 + 5;

        if (!lastCopied.empty()) {
            cvui::text(canvas, px, py, "Copied: " + lastCopied, 0.3, 0x00ff00);
        }
        py += 20;

        // ---- РАЗМЕР ----
        cvui::text(canvas, px, py, "Image size (px)", 0.4, 0xffffff);
        py += 18;

        cvui::text(canvas, px, py, "Width:", 0.35, 0xcccccc);
        cvui::counter(canvas, px + 50, py - 2, &inputW, 10, "%d");

        cvui::text(canvas, px + 150, py, "Height:", 0.35, 0xcccccc);
        cvui::counter(canvas, px + 210, py - 2, &inputH, 10, "%d");
        py += 28;

        if (cvui::button(canvas, px, py, 130, 22, "Apply size", 0.4)) {
            imgW = max(100, inputW);
            imgH = max(100, inputH);
        }
        if (cvui::button(canvas, px + 140, py, 130, 22, "Reset size", 0.4)) {
            imgW = 500; imgH = 400;
            inputW = 500; inputH = 400;
        }
        py += 30;

        // ---- ПРОСМОТР ----
        cvui::text(canvas, px, py, "View", 0.4, 0xffffff);
        py += 16;

        unsigned int svColor = splitView ? 0x006400 : 0x424242;
        if (cvui::button(canvas, px, py, 120, 22, "Split (Before/After)", 0.35, svColor)) {
            splitView = !splitView;
        }
        unsigned int singleColor = splitView ? 0x424242 : 0x006400;
        if (cvui::button(canvas, px + 125, py, 120, 22, "Single", 0.35, singleColor)) {
            splitView = false;
        }
        py += 28;

        // ---- РЕЖИМЫ ----
        cvui::text(canvas, px, py, "Mode", 0.4, 0xffffff);
        py += 16;

        if (cvui::button(canvas, px, py, 80, 20, "Original", 0.35)) { mode = 0; needRecalc = true; }
        if (cvui::button(canvas, px + 85, py, 80, 20, "Gray", 0.35)) { mode = 1; needRecalc = true; }
        if (cvui::button(canvas, px + 170, py, 80, 20, "Satur.", 0.35)) { mode = 2; needRecalc = true; }
        py += 23;
        if (cvui::button(canvas, px, py, 80, 20, "Hue", 0.35)) { mode = 3; needRecalc = true; }
        if (cvui::button(canvas, px + 85, py, 80, 20, "ColorMap", 0.35)) { mode = 4; needRecalc = true; }
        py += 28;

        // ---- МЕТОДЫ ----
        cvui::text(canvas, px, py, "Methods", 0.4, 0xffffff);
        py += 16;

        unsigned int rcColor = applyReplace ? 0x006400 : 0x424242;
        if (cvui::button(canvas, px, py, 120, 20, "Replace Color", 0.35, rcColor)) {
            applyReplace = !applyReplace;
            needRecalc = true;
        }

        unsigned int bcColor = applyBalance ? 0x006400 : 0x424242;
        if (cvui::button(canvas, px + 125, py, 120, 20, "Balance Colors", 0.35, bcColor)) {
            applyBalance = !applyBalance;
            needRecalc = true;
        }
        py += 28;

        // ---- ТРЕКБАРЫ ----
        cvui::text(canvas, px, py, "Adjustments", 0.4, 0xffffff);
        py += 18;

        int trackW = 130;

        cvui::text(canvas, px, py, "Bright", 0.3, 0xcccccc);
        cvui::text(canvas, px + 145, py, "Satur", 0.3, 0xcccccc);
        py += 10;
        if (cvui::trackbar(canvas, px, py, trackW, &brightness, 0, 200)) needRecalc = true;
        if (cvui::trackbar(canvas, px + 145, py, trackW, &saturation, 0, 200)) needRecalc = true;
        py += 42;

        cvui::text(canvas, px, py, "Contr", 0.3, 0xcccccc);
        cvui::text(canvas, px + 145, py, "Gamma", 0.3, 0xcccccc);
        py += 10;
        if (cvui::trackbar(canvas, px, py, trackW, &contrast, 0, 200)) needRecalc = true;
        if (cvui::trackbar(canvas, px + 145, py, trackW, &gammaVal, 50, 200)) needRecalc = true;
        py += 42;

        if (applyReplace) {
            cvui::text(canvas, px, py, "Target H", 0.3, 0xffff00);
            cvui::text(canvas, px + 145, py, "New H", 0.3, 0x00ffff);
            py += 10;
            if (cvui::trackbar(canvas, px, py, trackW, &targetHue, 0, 360)) needRecalc = true;
            if (cvui::trackbar(canvas, px + 145, py, trackW, &newHue, 0, 360)) needRecalc = true;
            py += 42;
        }

        // ---- КНОПКИ ----
        py += 5;
        if (cvui::button(canvas, px, py, 80, 25, "Save", 0.4)) {
            string savePath = saveFileDialog();
            if (!savePath.empty()) {
                if (imwrite(savePath, cachedDisplay)) {
                    cout << "Saved: " << savePath << endl;
                }
                else {
                    cout << "Save failed!" << endl;
                }
            }
        }

        if (cvui::button(canvas, px + 85, py, 80, 25, "Reset", 0.4)) {
            brightness = 100;
            saturation = 100;
            contrast = 100;
            gammaVal = 100;

            applyReplace = false;
            applyBalance = false;
            splitView = false;

            targetHue = 0;
            newHue = 180;
            mode = 0;

            imgW = 500;
            imgH = 400;
            inputW = 500;
            inputH = 400;

            lastCopied = "";
            needRecalc = true;
        }

        if (cvui::button(canvas, px + 170, py, 80, 25, "Exit", 0.4, 0x8B0000)) {
            break;
        }

        py += 30;
        if (cvui::button(canvas, px, py, 120, 25, "Reload image", 0.4)) {
            string newPath = openFileDialog();
            if (!newPath.empty()) {
                Mat ns = imread(newPath, IMREAD_COLOR);
                if (!ns.empty()) {
                    if (ns.cols > MAX_SRC || ns.rows > MAX_SRC) {
                        double k = min((double)MAX_SRC / ns.cols, (double)MAX_SRC / ns.rows);
                        resize(ns, ns, Size(), k, k);
                    }
                    src = ns;
                    cachedColors = getDominantColors(src, 6);
                    needRecalc = true;
                    cout << "Reloaded: " << src.cols << "x" << src.rows << endl;
                }
            }
        }

        cvui::update(WINDOW_NAME);
        cv::imshow(WINDOW_NAME, canvas);

        int key = cv::waitKey(20);
        if (key == 27) break;  // ESC

        // Проверка: закрыто ли окно пользователем
        if (cv::getWindowProperty(WINDOW_NAME, cv::WND_PROP_VISIBLE) < 1) {
            break;
        }
    }

    cachedColors.clear();
    src.release();
    cachedDisplay.release();
    destroyAllWindows();
    return 0;
}