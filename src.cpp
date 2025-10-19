#include <QtConcurrent>
#include <QDateTime>
#include <QDir>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

#include <cmath>
#include <random>
#include <vector>

static inline int clampInt(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static inline int quantizeLUTValue(int v, const std::vector<int> &lut) {
    return lut[v];
}

QImage vintageFilter(const QImage &src,
                     float intensity = 0.8f,
                     float vignette = 0.6f,
                     float grain = 0.04f,
                     float contrast = 0.15f)
{
    if (intensity <= 0.0f && vignette <= 0.0f && grain <= 0.0f && fabs(contrast) < 1e-6f)
        return src;

    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    const float maxDist = std::sqrt(cx*cx + cy*cy);

    const float contrastMul = 1.0f + contrast * 0.6f;

    std::mt19937 rng((unsigned)std::random_device{}());
    std::uniform_real_distribution<float> distUniform(-1.0f, 1.0f);

    const float toneAmount = 0.25f * intensity;
    const float desatAmount = 0.25f * intensity;
    const float vignetteAmount = vignette * intensity;
    const float grainAmount = grain;

    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            int a = qAlpha(p);
            float r = qRed(p);
            float g = qGreen(p);
            float b = qBlue(p);

            float lum = 0.299f * r + 0.587f * g + 0.114f * b;
            r = r * (1.0f - desatAmount) + lum * desatAmount;
            g = g * (1.0f - desatAmount) + lum * desatAmount;
            b = b * (1.0f - desatAmount) + lum * desatAmount;

            r = r + (255.0f - r) * (0.30f * toneAmount);
            g = g + (255.0f - g) * (0.12f * toneAmount);
            b = b * (1.0f - 0.20f * toneAmount);

            r = (r - 128.0f) * contrastMul + 128.0f;
            g = (g - 128.0f) * contrastMul + 128.0f;
            b = (b - 128.0f) * contrastMul + 128.0f;

            if (vignetteAmount > 0.0f) {
                float dx = x - cx;
                float dy = y - cy;
                float d = std::sqrt(dx*dx + dy*dy);
                float t = d / maxDist; // 0..1
                float vign = 1.0f - vignetteAmount * (t * t);
                if (vign < 0.0f) vign = 0.0f;
                r *= vign;
                g *= vign;
                b *= vign;
            }

            if (grainAmount > 0.0f) {
                float n = distUniform(rng) * grainAmount * 255.0f;
                r += n;
                g += n;
                b += n;
            }

            int nr = qBound(0, int(std::round(r)), 255);
            int ng = qBound(0, int(std::round(g)), 255);
            int nb = qBound(0, int(std::round(b)), 255);
            line[x] = qRgba(nr, ng, nb, a);
        }
    }

    return img;
}

QImage warmFilter(const QImage &src, float intensity = 0.6f)
{
    if (intensity <= 0.0f) return src;
    if (intensity > 1.0f) intensity = 1.0f;

    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
        const int w = img.width();
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            int a = qAlpha(p);
            int r = qRed(p);
            int g = qGreen(p);
            int b = qBlue(p);

            // повышаем R ближе к 255, чуть увеличиваем G, уменьшаем B
            int nr = int(std::round(r + (255 - r) * (0.45f * intensity)));
            int ng = int(std::round(g + (255 - g) * (0.20f * intensity)));
            int nb = int(std::round(b * (1.0f - 0.25f * intensity)));

            line[x] = qRgba(qBound(0, nr, 255), qBound(0, ng, 255), qBound(0, nb, 255), a);
        }
    }
    return img;
}

QImage coldFilter(const QImage &src, float intensity = 0.6f)
{
    if (intensity <= 0.0f) return src;
    if (intensity > 1.0f) intensity = 1.0f;

    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
        const int w = img.width();
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            int a = qAlpha(p);
            int r = qRed(p);
            int g = qGreen(p);
            int b = qBlue(p);

            int nb = int(std::round(b + (255 - b) * (0.45f * intensity)));
            int ng = int(std::round(g + (255 - g) * (0.12f * intensity)));
            int nr = int(std::round(r * (1.0f - 0.30f * intensity)));

            line[x] = qRgba(qBound(0, nr, 255), qBound(0, ng, 255), qBound(0, nb, 255), a);
        }
    }
    return img;
}

QImage posterizeEffect(const QImage &srcImage, int levels = 12, bool dither = false)
{
    if (levels < 2) levels = 2;
    QImage img = srcImage.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();

    std::vector<int> lut(256);
    const int steps = levels - 1;
    for (int v = 0; v < 256; ++v) {
        int idx = int(std::round((v * 1.0f * steps) / 255.0f));
        int quant = int(std::round((idx * 255.0f) / (float)steps));
        lut[v] = clampInt(quant);
    }

    if (!dither) {
        for (int y = 0; y < h; ++y) {
            QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb p = line[x];
                int a = qAlpha(p);
                int r = quantizeLUTValue(qRed(p), lut);
                int g = quantizeLUTValue(qGreen(p), lut);
                int b = quantizeLUTValue(qBlue(p), lut);
                line[x] = qRgba(r, g, b, a);
            }
        }
        return img;
    }

    struct RGBf { float r, g, b; unsigned char a; };
    std::vector<RGBf> buf(w * h);
    for (int y = 0; y < h; ++y) {
        const QRgb *sline = reinterpret_cast<const QRgb*>(srcImage.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = sline[x];
            RGBf &c = buf[y * w + x];
            c.r = static_cast<float>(qRed(p));
            c.g = static_cast<float>(qGreen(p));
            c.b = static_cast<float>(qBlue(p));
            c.a = static_cast<unsigned char>(qAlpha(p));
        }
    }
    QImage dst(w, h, QImage::Format_ARGB32);

    auto quantizeChannel = [&](float v, const std::vector<int> &lut) -> int {
        int vi = clampInt(int(std::round(v)));
        return lut[vi];
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            RGBf &c = buf[y * w + x];
            int oldR = clampInt(int(std::round(c.r)));
            int oldG = clampInt(int(std::round(c.g)));
            int oldB = clampInt(int(std::round(c.b)));

            int newR = quantizeChannel(c.r, lut);
            int newG = quantizeChannel(c.g, lut);
            int newB = quantizeChannel(c.b, lut);

            dst.setPixel(x, y, qRgba(newR, newG, newB, c.a));

            float errR = oldR - newR;
            float errG = oldG - newG;
            float errB = oldB - newB;

            auto addError = [&](int nx, int ny, float factor) {
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) return;
                RGBf &nc = buf[ny * w + nx];
                nc.r += errR * factor;
                nc.g += errG * factor;
                nc.b += errB * factor;
            };

            addError(x + 1, y    , 7.0f / 16.0f);
            addError(x - 1, y + 1, 3.0f / 16.0f);
            addError(x    , y + 1, 5.0f / 16.0f);
            addError(x + 1, y + 1, 1.0f / 16.0f);
        }
    }

    return dst;
}


QImage hardSolarizeInvert(const QImage &src, int threshold = 128)
{
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
        const int w = img.width();
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            int a = qAlpha(p);
            int r = qRed(p);
            int g = qGreen(p);
            int b = qBlue(p);

            int lum = qGray(p);
            if (lum > threshold) {
                line[x] = qRgba(255 - 3*r, 255 - 3*g, 255 - 3*b, a);
            }
        }
    }
    return img;
}

QImage toSepia(const QImage &srcImage) {
    QImage img = srcImage.convertToFormat(QImage::Format_ARGB32);

        const int h = img.height();
        for (int y = 0; y < h; ++y) {
            QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
            const int w = img.width();
            for (int x = 0; x < w; ++x) {
                QRgb p = line[x];
                int a = qAlpha(p);
                int r = qRed(p);
                int g = qGreen(p);
                int b = qBlue(p);

                int tr = qBound(0, static_cast<int>(0.393*r + 0.769*g + 0.189*b), 255);
                int tg = qBound(0, static_cast<int>(0.349*r + 0.686*g + 0.168*b), 255);
                int tb = qBound(0, static_cast<int>(0.272*r + 0.534*g + 0.131*b), 255);

                line[x] = qRgba(tr, tg, tb, a);
            }
        }
        return img;
}

static QString filterSlug(const QString &code) {
    static const QHash<QString, QString> mapping = {
        {QStringLiteral("бф"), QStringLiteral("no_filter")},
        {QStringLiteral("чб"), QStringLiteral("bw")},
        {QStringLiteral("нег"), QStringLiteral("negative")},
        {QStringLiteral("сеп"), QStringLiteral("sepia")},
        {QStringLiteral("пос"), QStringLiteral("posterize")},
        {QStringLiteral("сол"), QStringLiteral("solarize")},
        {QStringLiteral("хол"), QStringLiteral("cold")},
        {QStringLiteral("теп"), QStringLiteral("warm")},
        {QStringLiteral("вин"), QStringLiteral("vintage")}
    };

    const QString slug = mapping.value(code);
    if (!slug.isEmpty()) {
        return slug;
    }

    QString simplified = code.simplified();
    simplified.replace(QChar(' '), QChar('_'));
    QString sanitized;
    sanitized.reserve(simplified.size());
    for (const QChar ch : simplified) {
        if (ch.isLetterOrNumber() && ch.unicode() < 128) {
            sanitized.append(ch);
        }
    }
    if (sanitized.isEmpty()) {
        sanitized = QString::number(qHash(code));
    }
    return sanitized;
}

static QImage applyFilter(const QImage &source, const QString &type) {
    if (source.isNull()) {
        return source;
    }

    if (type == QStringLiteral("чб")) {
        return source.convertToFormat(QImage::Format_Grayscale8);
    }
    if (type == QStringLiteral("нег")) {
        QImage neg = source.convertToFormat(QImage::Format_ARGB32);
        neg.invertPixels(QImage::InvertRgb);
        return neg;
    }
    if (type == QStringLiteral("сеп")) {
        return toSepia(source);
    }
    if (type == QStringLiteral("пос")) {
        return posterizeEffect(source);
    }
    if (type == QStringLiteral("сол")) {
        return hardSolarizeInvert(source);
    }
    if (type == QStringLiteral("хол")) {
        return coldFilter(source);
    }
    if (type == QStringLiteral("теп")) {
        return warmFilter(source);
    }
    if (type == QStringLiteral("вин")) {
        return vintageFilter(source);
    }

    return source;
}

void setpic(QImage *img, QLabel *lbl, QString type) {
    if (!lbl) {
        return;
    }
    if (!img || img->isNull()) {
        lbl->clear();
        return;
    }

    const QImage filtered = applyFilter(*img, type);
    QPixmap pix = QPixmap::fromImage(filtered);
    pix = pix.scaled(640, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    lbl->setPixmap(pix);
}

QList<QFuture<bool>> saveFilteredImages(const QImage &sourceImage, QList<QString> filters, const QString &directory) {
    QList<QFuture<bool>> tasks;
    if (sourceImage.isNull()) {
        return tasks;
    }
    if (filters.isEmpty()) {
        return tasks;
    }

    filters.removeDuplicates();

    QDir targetDir(directory);
    if (!targetDir.exists()) {
        targetDir.mkpath(QStringLiteral("."));
    }

    const QString baseName = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    int index = 0;

    for (const QString &code : filters) {
        const QString slug = filterSlug(code);
        const QString fileName = QStringLiteral("%1_%2_%3.png")
                                     .arg(baseName)
                                     .arg(index, 2, 10, QLatin1Char('0'))
                                     .arg(slug.isEmpty() ? QStringLiteral("image") : slug);
        const QString filePath = targetDir.filePath(fileName);
        const QImage processed = applyFilter(sourceImage, code).convertToFormat(QImage::Format_ARGB32);

        tasks.append(QtConcurrent::run([processed, filePath]() -> bool {
            if (filePath.isEmpty()) {
                return false;
            }
            const bool ok = processed.save(filePath, "PNG");
            if (!ok) {
                qWarning() << "Не удалось сохранить" << filePath;
            }
            return ok;
        }));

        ++index;
    }

    return tasks;
}

static QString ffmpegFilterForCode(const QString &code) {
    if (code == QStringLiteral("чб")) {
        return QStringLiteral("format=gray");
    }
    if (code == QStringLiteral("нег")) {
        return QStringLiteral("negate");
    }
    if (code == QStringLiteral("сеп")) {
        return QStringLiteral("colorchannelmixer=.393:.769:.189:0:.349:.686:.168:0:.272:.534:.131");
    }
    if (code == QStringLiteral("пос")) {
        return QStringLiteral("lutrgb=r='floor(val/64)*64':g='floor(val/64)*64':b='floor(val/64)*64'");
    }
    if (code == QStringLiteral("сол")) {
        return QStringLiteral("lutyuv=y='if(lt(val,128),val,255-val)'");
    }
    if (code == QStringLiteral("хол")) {
        return QStringLiteral("colorbalance=bs=0.35:rs=-0.25");
    }
    if (code == QStringLiteral("теп")) {
        return QStringLiteral("colorbalance=rs=0.35:bs=-0.25");
    }
    if (code == QStringLiteral("вин")) {
        return QStringLiteral("curves=blue='0/0 0.5/0.4 1/1',vignette=PI/3");
    }
    return QString();
}

QList<QFuture<bool>> saveFilteredVideos(const QString &videoPath, QList<QString> filters, const QString &directory) {
    QList<QFuture<bool>> tasks;
    if (videoPath.isEmpty() || !QFile::exists(videoPath)) {
        return tasks;
    }
    if (filters.isEmpty()) {
        return tasks;
    }

    filters.removeDuplicates();

    QDir targetDir(directory);
    if (!targetDir.exists()) {
        targetDir.mkpath(QStringLiteral("."));
    }

    const QString ffmpegPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const QString baseName = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    int index = 0;

    for (const QString &code : filters) {
        const QString slug = filterSlug(code);
        const QString fileName = QStringLiteral("%1_%2_%3.mp4")
                                     .arg(baseName)
                                     .arg(index, 2, 10, QLatin1Char('0'))
                                     .arg(slug.isEmpty() ? QStringLiteral("video") : slug);
        const QString filePath = targetDir.filePath(fileName);

        tasks.append(QtConcurrent::run([videoPath, filePath, code, ffmpegPath]() -> bool {
            if (filePath.isEmpty()) {
                return false;
            }

            auto cleanTarget = [&filePath]() -> bool {
                if (QFile::exists(filePath) && !QFile::remove(filePath)) {
                    qWarning() << "Не удалось перезаписать" << filePath;
                    return false;
                }
                return true;
            };

            if (!cleanTarget()) {
                return false;
            }

            const QString filterExpr = ffmpegFilterForCode(code);

            if (ffmpegPath.isEmpty() || filterExpr.isEmpty()) {
                if (!QFile::copy(videoPath, filePath)) {
                    qWarning() << "Не удалось сохранить" << filePath;
                    return false;
                }
                return true;
            }

            QStringList arguments;
            arguments << QStringLiteral("-y")
                      << QStringLiteral("-i") << videoPath
                      << QStringLiteral("-vf") << filterExpr
                      << QStringLiteral("-c:v") << QStringLiteral("libx264")
                      << QStringLiteral("-preset") << QStringLiteral("veryfast")
                      << QStringLiteral("-crf") << QStringLiteral("22")
                      << QStringLiteral("-c:a") << QStringLiteral("copy")
                      << filePath;

            QProcess process;
            process.start(ffmpegPath, arguments, QIODevice::ReadOnly);
            const bool started = process.waitForStarted();
            if (!started) {
                qWarning() << "Не удалось запустить ffmpeg для фильтра" << code;
                return false;
            }

            if (!process.waitForFinished(-1)) {
                qWarning() << "ffmpeg не завершился корректно для фильтра" << code;
                return false;
            }

            const int exitCode = process.exitCode();
            if (exitCode != 0) {
                qWarning() << "ffmpeg завершился с ошибкой" << exitCode << "для фильтра" << code;
                return false;
            }

            return QFile::exists(filePath);
        }));

        ++index;
    }

    return tasks;
}
