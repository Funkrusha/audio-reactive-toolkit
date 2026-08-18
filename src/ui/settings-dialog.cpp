// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings-dialog.hpp"

#include <obs-module.h>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QFont>
#include <QLabel>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace {
QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}
} // namespace

SettingsDialogResult
show_settings_dialog(void *parent, const std::vector<std::string> &audio_sources,
		     const std::vector<ObsTextSourceOption> &text_sources, const std::string &selected_source,
		     const std::string &selected_bpm_text_source, const std::string &selected_bpm_text_source_uuid,
		     const std::string &bpm_text_format, uint32_t bpm_decimal_places, uint16_t websocket_port,
		     uint32_t websocket_messages_per_second, uint32_t fft_size, uint32_t beat_sensitivity,
		     uint32_t beat_cooldown_ms, uint32_t transient_sensitivity, uint32_t transient_cooldown_ms,
		     bool debug_logging)
{
	auto *parent_widget = static_cast<QWidget *>(parent);
	QDialog dialog(parent_widget);
	dialog.setWindowTitle(text("Settings.Title"));
	dialog.setMinimumWidth(540);
	dialog.setModal(true);

	auto *layout = new QVBoxLayout(&dialog);
	layout->setContentsMargins(24, 22, 24, 22);
	layout->setSpacing(16);

	auto *title = new QLabel(QStringLiteral("Audio Reactive Toolkit"), &dialog);
	title->setObjectName(QStringLiteral("dialogTitle"));
	QFont title_font = title->font();
	title_font.setPointSize(title_font.pointSize() + 5);
	title_font.setBold(true);
	title->setFont(title_font);
	auto *subtitle = new QLabel(text("Settings.Subtitle"), &dialog);
	subtitle->setObjectName(QStringLiteral("dialogSubtitle"));
	subtitle->setWordWrap(true);
	layout->addWidget(title);
	layout->addWidget(subtitle);

	auto *separator = new QFrame(&dialog);
	separator->setFrameShape(QFrame::HLine);
	layout->addWidget(separator);

	auto *form = new QFormLayout();
	form->setHorizontalSpacing(18);
	form->setVerticalSpacing(14);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

	auto *source_combo = new QComboBox(&dialog);
	source_combo->setMinimumContentsLength(32);
	source_combo->addItem(text("Settings.AudioSource.Automatic"), QString());
	int selected_index = 0;
	for (const auto &source : audio_sources) {
		const QString name = QString::fromUtf8(source.c_str());
		source_combo->addItem(name, name);
		if (source == selected_source)
			selected_index = source_combo->count() - 1;
	}
	source_combo->setCurrentIndex(selected_index);
	form->addRow(text("Settings.AudioSource"), source_combo);

	auto *bpm_text_source_combo = new QComboBox(&dialog);
	bpm_text_source_combo->setMinimumContentsLength(32);
	bpm_text_source_combo->addItem(text("Settings.BpmTextSource.None"), QString());
	int selected_bpm_text_index = 0;
	for (const auto &source : text_sources) {
		const QString name = QString::fromUtf8(source.name.c_str());
		const QString uuid = QString::fromUtf8(source.uuid.c_str());
		bpm_text_source_combo->addItem(name, uuid);
		if ((!selected_bpm_text_source_uuid.empty() && source.uuid == selected_bpm_text_source_uuid) ||
		    (selected_bpm_text_source_uuid.empty() && source.name == selected_bpm_text_source))
			selected_bpm_text_index = bpm_text_source_combo->count() - 1;
	}
	bpm_text_source_combo->setCurrentIndex(selected_bpm_text_index);
	form->addRow(text("Settings.BpmTextSource"), bpm_text_source_combo);

	auto *bpm_text_format_edit = new QLineEdit(QString::fromUtf8(bpm_text_format.c_str()), &dialog);
	bpm_text_format_edit->setPlaceholderText(QStringLiteral("{bpm} BPM"));
	bpm_text_format_edit->setToolTip(text("Settings.BpmTextFormat.Tooltip"));
	form->addRow(text("Settings.BpmTextFormat"), bpm_text_format_edit);

	auto *bpm_decimal_places_combo = new QComboBox(&dialog);
	for (const uint32_t places : {0U, 1U, 2U}) {
		bpm_decimal_places_combo->addItem(
			text(places == 1 ? "Settings.DecimalPlace.One" : "Settings.DecimalPlace.Many").arg(places),
			places);
		if (places == bpm_decimal_places)
			bpm_decimal_places_combo->setCurrentIndex(bpm_decimal_places_combo->count() - 1);
	}
	form->addRow(text("Settings.BpmRounding"), bpm_decimal_places_combo);

	auto *fft_combo = new QComboBox(&dialog);
	for (const uint32_t size : {2048U, 4096U, 8192U, 16384U}) {
		fft_combo->addItem(QString::number(size), size);
		if (size == fft_size)
			fft_combo->setCurrentIndex(fft_combo->count() - 1);
	}
	form->addRow(text("Settings.FftSize"), fft_combo);

	auto *port_spin = new QSpinBox(&dialog);
	port_spin->setRange(1024, 65535);
	port_spin->setValue(websocket_port);
	port_spin->setAccelerated(true);
	form->addRow(text("Settings.WebSocketPort"), port_spin);

	auto *message_rate_spin = new QSpinBox(&dialog);
	message_rate_spin->setRange(1, 60);
	message_rate_spin->setValue(static_cast<int>(websocket_messages_per_second));
	message_rate_spin->setSuffix(QStringLiteral(" / s"));
	form->addRow(text("Settings.WebSocketMessages"), message_rate_spin);

	auto *logging_combo = new QComboBox(&dialog);
	logging_combo->addItem(text("Settings.Logging.Info"), false);
	logging_combo->addItem(text("Settings.Logging.Debug"), true);
	logging_combo->setCurrentIndex(debug_logging ? 1 : 0);
	form->addRow(text("Settings.Logging"), logging_combo);
	layout->addLayout(form);

	auto *detection_group = new QGroupBox(text("Settings.Detection"), &dialog);
	auto *detection_form = new QFormLayout(detection_group);
	detection_form->setHorizontalSpacing(18);
	detection_form->setVerticalSpacing(12);
	detection_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

	auto make_spin = [&dialog](int minimum, int maximum, int value, const QString &suffix) {
		auto *spin = new QSpinBox(&dialog);
		spin->setRange(minimum, maximum);
		spin->setValue(value);
		spin->setSuffix(suffix);
		spin->setAccelerated(true);
		return spin;
	};
	auto *beat_sensitivity_spin = make_spin(25, 200, static_cast<int>(beat_sensitivity), QStringLiteral(" %"));
	auto *beat_cooldown_spin = make_spin(80, 1000, static_cast<int>(beat_cooldown_ms), QStringLiteral(" ms"));
	auto *transient_sensitivity_spin =
		make_spin(25, 200, static_cast<int>(transient_sensitivity), QStringLiteral(" %"));
	auto *transient_cooldown_spin =
		make_spin(20, 500, static_cast<int>(transient_cooldown_ms), QStringLiteral(" ms"));
	detection_form->addRow(text("Settings.BeatSensitivity"), beat_sensitivity_spin);
	detection_form->addRow(text("Settings.BeatCooldown"), beat_cooldown_spin);
	detection_form->addRow(text("Settings.TransientSensitivity"), transient_sensitivity_spin);
	detection_form->addRow(text("Settings.TransientCooldown"), transient_cooldown_spin);
	layout->addWidget(detection_group);

	auto *hint = new QLabel(text("Settings.WebSocketHint"), &dialog);
	hint->setObjectName(QStringLiteral("hint"));
	hint->setWordWrap(true);
	layout->addWidget(hint);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Save)->setText(text("Settings.Save"));
	buttons->button(QDialogButtonBox::Cancel)->setText(text("Settings.Cancel"));
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	SettingsDialogResult result;
	result.accepted = dialog.exec() == QDialog::Accepted;
	if (!result.accepted)
		return result;

	const QByteArray source_utf8 = source_combo->currentData().toString().toUtf8();
	result.source_name.assign(source_utf8.constData(), static_cast<size_t>(source_utf8.size()));
	const QByteArray bpm_text_source_name_utf8 = bpm_text_source_combo->currentIndex() > 0
							     ? bpm_text_source_combo->currentText().toUtf8()
							     : QByteArray();
	result.bpm_text_source_name.assign(bpm_text_source_name_utf8.constData(),
					   static_cast<size_t>(bpm_text_source_name_utf8.size()));
	const QByteArray bpm_text_source_uuid_utf8 = bpm_text_source_combo->currentData().toString().toUtf8();
	result.bpm_text_source_uuid.assign(bpm_text_source_uuid_utf8.constData(),
					   static_cast<size_t>(bpm_text_source_uuid_utf8.size()));
	const QByteArray bpm_text_format_utf8 = bpm_text_format_edit->text().trimmed().toUtf8();
	result.bpm_text_format.assign(bpm_text_format_utf8.constData(),
				      static_cast<size_t>(bpm_text_format_utf8.size()));
	if (result.bpm_text_format.empty())
		result.bpm_text_format = "{bpm} BPM";
	result.bpm_decimal_places = bpm_decimal_places_combo->currentData().toUInt();
	result.websocket_port = static_cast<uint16_t>(port_spin->value());
	result.websocket_messages_per_second = static_cast<uint32_t>(message_rate_spin->value());
	result.fft_size = fft_combo->currentData().toUInt();
	result.beat_sensitivity = static_cast<uint32_t>(beat_sensitivity_spin->value());
	result.beat_cooldown_ms = static_cast<uint32_t>(beat_cooldown_spin->value());
	result.transient_sensitivity = static_cast<uint32_t>(transient_sensitivity_spin->value());
	result.transient_cooldown_ms = static_cast<uint32_t>(transient_cooldown_spin->value());
	result.debug_logging = logging_combo->currentData().toBool();
	return result;
}
