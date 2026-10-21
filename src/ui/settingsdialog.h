#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// 所有"不常改"的选项都在这里，主界面保持只有拖放和结果
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    void accept() override;

private:
    void loadProvider(const QString& id);
    void refreshModelStatus();
    void refreshCache();

    QComboBox* m_provider;
    QLineEdit* m_baseUrl;
    QLineEdit* m_model;
    QLineEdit* m_key;
    QPushButton* m_clearKey;
    QLabel* m_keyStatus;
    QComboBox* m_asrModel;
    QLabel* m_asrStatus;
    QPushButton* m_download;
    QComboBox* m_format;
    QComboBox* m_layout;
    QLabel* m_cacheLabel;
    QComboBox* m_position;
    QComboBox* m_size;
    QCheckBox* m_furigana;
    QPlainTextEdit* m_glossary;
    QCheckBox* m_style;
    QCheckBox* m_lyrics;
    QCheckBox* m_review;
    QString m_loadedProvider;
};
