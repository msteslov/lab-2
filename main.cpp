#include <QtWidgets>
#include <QtMultimedia>
#include <QtMultimediaWidgets>
#include <QtConcurrent>
#include <cstdlib>
#include <memory>
#include "src.cpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QWidget *w = new QWidget();
    w->resize(800, 600);

    QCamera *camera = nullptr;
    const QList<QCameraInfo> cameras = QCameraInfo::availableCameras();
    if (cameras.isEmpty()) {
        qDebug() << "Камеры нет";
        return EXIT_FAILURE;
    }

    camera = new QCamera(cameras.first(), w);
    w->show();

    QCameraViewfinder *viewfinder = new QCameraViewfinder(w);
    camera->setViewfinder(viewfinder);

    QCameraImageCapture *imageCapture = new QCameraImageCapture(camera);
    QMediaRecorder *mediaRecorder = new QMediaRecorder(camera);
    camera->setCaptureMode(QCamera::CaptureStillImage);
    camera->start();

    auto lastRecordedVideoPath = std::make_shared<QString>();
    auto recordingActive = std::make_shared<bool>(false);


    QGridLayout *mn = new QGridLayout();
    QVBoxLayout *vb = new QVBoxLayout();
    QList<QString> filters;

    auto *btn1 = new QCheckBox("Без фильтра");
    auto *btn2 = new QCheckBox("ЧБ");
    auto *btn3 = new QCheckBox("Сепия");
    auto *btn4 = new QCheckBox("Негатив");
    auto *btn5 = new QCheckBox("Постеризация");
    auto *btn6 = new QCheckBox("Соляризация");
    auto *btn7 = new QCheckBox("Холодный");
    auto *btn8 = new QCheckBox("Теплый");
    auto *btn9 = new QCheckBox("Винтаж");
    auto *shelk = new QPushButton("Снимок");
    auto *recordButton = new QPushButton("Видео");

    auto registerFilterToggle = [&filters](QCheckBox *box, const QString &code) {
        QObject::connect(box, &QCheckBox::toggled, [code, &filters](bool checked) {
            if (checked) {
                if (!filters.contains(code)) {
                    filters.append(code);
                }
            } else {
                filters.removeAll(code);
            }
        });
    };

    registerFilterToggle(btn1, QStringLiteral("бф"));
    registerFilterToggle(btn2, QStringLiteral("чб"));
    registerFilterToggle(btn3, QStringLiteral("сеп"));
    registerFilterToggle(btn4, QStringLiteral("нег"));
    registerFilterToggle(btn5, QStringLiteral("пос"));
    registerFilterToggle(btn6, QStringLiteral("сол"));
    registerFilterToggle(btn7, QStringLiteral("хол"));
    registerFilterToggle(btn8, QStringLiteral("теп"));
    registerFilterToggle(btn9, QStringLiteral("вин"));


    QObject::connect(shelk, &QPushButton::clicked, [imageCapture](bool) {
        imageCapture->capture();

    });
    QObject::connect(recordButton, &QPushButton::clicked, w, [camera, mediaRecorder, recordButton, recordingActive, lastRecordedVideoPath]() {
        if (!recordButton) {
            return;
        }

        if (!*recordingActive) {
            QString baseDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
            if (baseDir.isEmpty()) {
                baseDir = QDir::tempPath();
            }
            QDir dir(baseDir);
            if (!dir.exists()) {
                dir.mkpath(QStringLiteral("."));
            }

            const QString fileName = QStringLiteral("video_%1.mp4").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
            const QString outputPath = dir.filePath(fileName);
            mediaRecorder->setOutputLocation(QUrl::fromLocalFile(outputPath));
            *lastRecordedVideoPath = outputPath;

            camera->setCaptureMode(QCamera::CaptureVideo);
            recordButton->setEnabled(false);
            recordButton->setText(QStringLiteral("Стоп"));
            mediaRecorder->record();
        } else {
            recordButton->setEnabled(false);
            mediaRecorder->stop();
        }
    });

    auto showVideoDialog = [w, &filters](const QString &videoPath) {
        if (videoPath.isEmpty() || !QFile::exists(videoPath)) {
            QMessageBox::warning(w, QStringLiteral("Видео недоступно"), QStringLiteral("Не удалось получить записанное видео."));
            return;
        }

        QWidget *dialog = new QWidget();
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(QStringLiteral("Видео готово"));
        dialog->resize(820, 520);

        auto *layout = new QVBoxLayout(dialog);
        auto *videoWidget = new QVideoWidget(dialog);
        layout->addWidget(videoWidget, /*stretch*/ 1);

        QMediaPlayer *player = new QMediaPlayer(dialog);
        player->setVideoOutput(videoWidget);
        player->setMedia(QUrl::fromLocalFile(videoPath));
        player->play();

        auto *controls = new QHBoxLayout();
        auto *replayButton = new QPushButton(QStringLiteral("Повтор"), dialog);
        auto *saveButton = new QPushButton(QStringLiteral("Сохранить"), dialog);
        controls->addWidget(replayButton);
        controls->addStretch();
        controls->addWidget(saveButton);
        layout->addLayout(controls);

        QObject::connect(replayButton, &QPushButton::clicked, player, &QMediaPlayer::play);

        QObject::connect(saveButton, &QPushButton::clicked, dialog, [dialog, saveButton, videoPath, &filters]() {
            QList<QString> selectedFilters = filters;
            selectedFilters.removeDuplicates();
            if (selectedFilters.isEmpty()) {
                QMessageBox::information(dialog, QStringLiteral("Нет фильтров"), QStringLiteral("Отметьте хотя бы один фильтр для сохранения."));
                return;
            }

            const QString directory = QFileDialog::getExistingDirectory(dialog, QStringLiteral("Выберите папку для сохранения"));
            if (directory.isEmpty()) {
                return;
            }

            const QList<QFuture<bool>> tasks = saveFilteredVideos(videoPath, selectedFilters, directory);
            if (tasks.isEmpty()) {
                QMessageBox::information(dialog, QStringLiteral("Нечего сохранять"), QStringLiteral("Не удалось подготовить видео для сохранения."));
                return;
            }

            QPointer<QPushButton> savePtr(saveButton);
            saveButton->setEnabled(false);

            auto completed = std::make_shared<int>(0);
            auto failures = std::make_shared<int>(0);
            const int totalTasks = tasks.size();

            for (const auto &task : tasks) {
                auto *watcher = new QFutureWatcher<bool>(dialog);
                QObject::connect(watcher, &QFutureWatcher<bool>::finished, dialog, [dialog, savePtr, completed, failures, totalTasks, watcher]() {
                    const bool ok = !watcher->isCanceled() && watcher->future().result();
                    watcher->deleteLater();
                    if (!ok) {
                        ++(*failures);
                    }

                    if (++(*completed) == totalTasks) {
                        if (savePtr) {
                            savePtr->setEnabled(true);
                        }

                        if (*failures == 0) {
                            QMessageBox::information(dialog, QStringLiteral("Готово"), QStringLiteral("Отфильтрованные видео сохранены."));
                        } else if (*failures == totalTasks) {
                            QMessageBox::critical(dialog, QStringLiteral("Ошибка"), QStringLiteral("Не удалось сохранить выбранные видео."));
                        } else {
                            QMessageBox::warning(dialog, QStringLiteral("Частично сохранено"), QStringLiteral("Часть видео сохранить не удалось."));
                        }
                    }
                });
                watcher->setFuture(task);
            }
        });

        dialog->show();
    };

    QObject::connect(mediaRecorder, &QMediaRecorder::stateChanged, w, [camera, recordButton, recordingActive, lastRecordedVideoPath, showVideoDialog](QMediaRecorder::State state) {
        if (!recordButton) {
            return;
        }

        switch (state) {
        case QMediaRecorder::RecordingState:
            *recordingActive = true;
            recordButton->setEnabled(true);
            recordButton->setText(QStringLiteral("Стоп"));
            break;
        case QMediaRecorder::StoppedState: {
            const bool wasRecording = *recordingActive;
            *recordingActive = false;
            recordButton->setEnabled(true);
            recordButton->setText(QStringLiteral("Видео"));
            camera->setCaptureMode(QCamera::CaptureStillImage);
            camera->start();

            if (wasRecording && !lastRecordedVideoPath->isNull() && QFile::exists(*lastRecordedVideoPath)) {
                showVideoDialog(*lastRecordedVideoPath);
            }
            break;
        }
        default:
            break;
        }
    });

    QObject::connect(mediaRecorder, QOverload<QMediaRecorder::Error>::of(&QMediaRecorder::error), w, [recordButton](QMediaRecorder::Error error) {
        if (!recordButton) {
            return;
        }
        if (error != QMediaRecorder::NoError) {
            recordButton->setEnabled(true);
            recordButton->setText(QStringLiteral("Видео"));
        }
    });

    auto type = std::make_shared<QString>("бф");
    QObject::connect(imageCapture, &QCameraImageCapture::imageSaved, [w, &filters, type](int, const QString &tof) {
        QWidget *w2 = new QWidget();
        QVBoxLayout *mn = new QVBoxLayout(w2);
        QHBoxLayout *hblt = new QHBoxLayout();
        auto *save = new QPushButton("Сохранить");
        QLabel *lbl = new QLabel();
        QImage *img = new QImage(tof);
        setpic(img, lbl, *type);
        QObject::connect(save, &QPushButton::clicked, [w, w2, save, img, &filters]() {
            if (!img || img->isNull()) {
                QMessageBox::warning(w2, QStringLiteral("Нет данных"), QStringLiteral("Нет снимка для сохранения."));
                return;
            }

            QList<QString> selectedFilters = filters;
            selectedFilters.removeDuplicates();
            if (selectedFilters.isEmpty()) {
                QMessageBox::information(w2, QStringLiteral("Нет фильтров"), QStringLiteral("Отметьте хотя бы один фильтр для сохранения."));
                return;
            }

            const QString directory = QFileDialog::getExistingDirectory(w2, QStringLiteral("Выберите папку для сохранения"));
            if (directory.isEmpty()) {
                return;
            }

            const QList<QFuture<bool>> tasks = saveFilteredImages(*img, selectedFilters, directory);
            if (tasks.isEmpty()) {
                QMessageBox::information(w2, QStringLiteral("Нечего сохранять"), QStringLiteral("Не удалось подготовить изображения для сохранения."));
                return;
            }

            QPointer<QPushButton> saveButton(save);
            save->setEnabled(false);

            auto completed = std::make_shared<int>(0);
            auto failures = std::make_shared<int>(0);
            const int totalTasks = tasks.size();

            for (const auto &task : tasks) {
                auto *watcher = new QFutureWatcher<bool>(w);
                QObject::connect(watcher, &QFutureWatcher<bool>::finished, w, [w, saveButton, completed, failures, totalTasks, watcher]() {
                    const bool ok = !watcher->isCanceled() && watcher->future().result();
                    if (!ok) {
                        ++(*failures);
                    }

                    watcher->deleteLater();
                    if (++(*completed) == totalTasks) {
                        if (saveButton) {
                            saveButton->setEnabled(true);
                        }
                        if (*failures == 0) {
                            QMessageBox::information(w, QStringLiteral("Готово"), QStringLiteral("Выбранные изображения сохранены."));
                        } else if (*failures == totalTasks) {
                            QMessageBox::critical(w, QStringLiteral("Ошибка"), QStringLiteral("Не удалось сохранить выбранные изображения."));
                        } else {
                            QMessageBox::warning(w, QStringLiteral("Частично сохранено"), QStringLiteral("Часть изображений сохранить не удалось."));
                        }
                    }
                });
                watcher->setFuture(task);
            }
        });

        auto *right_btn = new QPushButton("->");
        hblt -> addWidget(right_btn);

        QObject::connect(right_btn, &QPushButton::clicked, [img, lbl, type, &filters](bool){
            if (filters.isEmpty()) {
                return;
            }

            int currentIndex = filters.indexOf(*type);
            if (currentIndex < 0) {
                currentIndex = 0;
            } else {
                currentIndex = (currentIndex + 1) % filters.size();
            }

            *type = filters[currentIndex];
            setpic(img, lbl, *type);
        });



        hblt -> addStretch();
        hblt -> addWidget(lbl);
        hblt -> addStretch();
        mn -> addLayout(hblt);
        mn -> addWidget(save);

        w2 -> setWindowTitle("Файл сохранен");
        w2->setFixedSize(740, 450);
        w2 -> setLayout(mn);
        w2->show();
    });

    vb->addStretch(0);
    vb->addWidget(btn1);
    vb->addWidget(btn2);
    vb->addWidget(btn3);
    vb->addWidget(btn4);
    vb->addWidget(btn5);
    vb->addWidget(btn6);
    vb->addWidget(btn7);
    vb->addWidget(btn8);
    vb->addWidget(btn9);
    vb->addStretch(0);

    mn->addWidget(viewfinder, 0, 0);
    mn->addLayout(vb, 0, 1);
    mn->addWidget(shelk, 1, 0);
    mn->addWidget(recordButton, 1, 1);

    w->setLayout(mn);
    return app.exec();
}
