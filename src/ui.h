#pragma once

#include "qt.h"
#include "state.h"
#include "control.h"

#include <QLineEdit>
#include <QKeyEvent>
#include <QFocusEvent>

// ---- Hotkey capture ----
// Plain data: what the user has pressed so far in the settings box.
struct HotkeyCaptureState
{
	HotkeyConfig Captured;
	bool         HasCapture;
};

// Minimal QObject subtype used purely as a Qt event filter — no Q_OBJECT, no
// signals, no slots, no constructors with logic.  The only reason this inherits
// QObject at all is that installEventFilter() requires a QObject*.
struct HotkeyEventFilter : QObject
{
	HotkeyCaptureState *State;
	QLineEdit          *Edit;

	bool eventFilter(QObject * /*Watched*/, QEvent *Event) override
	{
		if (Event->type() == QEvent::KeyPress)
		{
			QKeyEvent *KeyEv = static_cast<QKeyEvent *>(Event);
			Qt::KeyboardModifiers Mods = KeyEv->modifiers();
			Qt::Key               Key  = Qt::Key(KeyEv->key());

			bool IsModifierOnly = (Key == Qt::Key_Control ||
			                       Key == Qt::Key_Alt     ||
			                       Key == Qt::Key_Shift   ||
			                       Key == Qt::Key_Meta);

			if (Key == Qt::Key_Escape)
			{
				State->HasCapture = false;
				State->Captured   = HotkeyConfig{};
				Edit->clear();
				return true;
			}

			if (Mods == Qt::NoModifier && !IsModifierOnly) return false;

			HotkeyConfig Config;
			Config.Modifiers  = Mods;
			Config.Key        = IsModifierOnly ? Qt::Key(0) : Key;
			State->Captured   = Config;
			State->HasCapture = true;
			Edit->setText(Config.to_label());
			return true;
		}

		if (Event->type() == QEvent::FocusIn)
		{
			Edit->setStyleSheet("border: 2px solid #0d47a1; background: #1565c0; color: black;");
			return false;
		}

		if (Event->type() == QEvent::FocusOut)
		{
			Edit->setStyleSheet("");
			return false;
		}

		return false;
	}
};

// Allocates and wires a hotkey-capture QLineEdit.
// The filter's lifetime is tied to Edit (Edit is its QObject parent).
inline
void
make_hotkey_capture_edit(
	QWidget            *Parent,
	HotkeyCaptureState *State,
	QLineEdit         **OutEdit,
	const HotkeyConfig &Initial)
{
	QLineEdit *Edit = new QLineEdit(Parent);
	Edit->setReadOnly(true);
	Edit->setPlaceholderText("Click here, then press your hotkey...");
	Edit->setAlignment(Qt::AlignCenter);

	State->Captured   = Initial;
	State->HasCapture = Initial.is_valid();
	if (State->HasCapture) Edit->setText(Initial.to_label());

	HotkeyEventFilter *Filter = new HotkeyEventFilter();
	Filter->State = State;
	Filter->Edit  = Edit;
	Edit->installEventFilter(Filter);
	// Give Edit ownership so the filter is destroyed with the widget
	Filter->setParent(Edit);

	*OutEdit = Edit;
}

// ---- Settings Window ----
// Layout:
//   Row 0 : "Keyboard Shortcuts" heading
//   Row 1 : [Record btn] [Stream btn] [Load/Unload Model btn]  — action selector
//   Row 2 : "Current: <label>"
//   Row 3 : hotkey capture box
//   Row 4 : hint text
//   Row 5 : [Set Hotkey btn] [Close btn]

// Shared state threaded through the lambda callbacks.
struct SettingsWindowState
{
	// Which action is selected (0=Record, 1=Stream, 2=LoadModel)
	int             SelectedAction;

	// Capture box state
	HotkeyCaptureState Capture;

	// Temporary hotkey configs (committed on Save)
	HotkeyConfig TempHotkeys[4];

	// Temporary settings (committed on Save)
	bool TempPlayRecordSound;
	bool TempUseCharByCharInjection;
	int TempWhisperThreadCount;

	// Pointers back into the dialog so callbacks can refresh UI
	QLabel     *CurrentLabel;
	QLineEdit  *CaptureEdit;
	QPushButton *ActionButtons[4];
	QCheckBox  *SoundCheckBox;
	QCheckBox  *CharByCharCheckBox;
	QSpinBox   *ThreadCountSpinner;
};

inline
void
settings_select_action(SettingsWindowState *S, int Action)
{
	S->SelectedAction = Action;

	// Highlight the selected button, un-highlight the others
	for (int I = 0; I < 4; I++)
	{
		S->ActionButtons[I]->setStyleSheet(
			I == Action ? BUTTON_STYLE_BLUE : "");
	}

	// Show the current hotkey for the selected action in the capture box
	HotkeyConfig Current = S->TempHotkeys[Action];
	S->CurrentLabel->setText(QString("Current: %1").arg(Current.to_label()));

	// Reset capture box to the temp hotkey for this action
	S->Capture.Captured   = Current;
	S->Capture.HasCapture = Current.is_valid();
	S->CaptureEdit->setText(Current.to_label());
}

inline
void
open_settings_window(GlobalState *AppState)
{
	AppState->IsSettingsDialogOpen = true;

	QDialog *Dialog = new QDialog(AppState->QtMainWindow);
	Dialog->setWindowTitle("Settings");
	Dialog->setModal(true);
	Dialog->resize(620, 320);

	QGridLayout *Layout = new QGridLayout(Dialog);
	int Row = 0;

	// Sound checkbox
	QCheckBox *SoundCheckBox = new QCheckBox("Play sound when starting/stopping recording", Dialog);
	Layout->addWidget(SoundCheckBox, Row++, 0, 1, 4);

	// Text injection method checkbox
	QCheckBox *CharByCharCheckBox = new QCheckBox("Use character-by-character text injection (instead of paste)", Dialog);
	Layout->addWidget(CharByCharCheckBox, Row++, 0, 1, 4);

	// CPU Cores for Inference
	QHBoxLayout *ThreadCountRow = new QHBoxLayout();
	QLabel *ThreadCountLabel = new QLabel("CPU Cores for Inference:", Dialog);
	QSpinBox *ThreadCountSpinner = new QSpinBox(Dialog);
	ThreadCountSpinner->setMinimum(1);
	ThreadCountSpinner->setMaximum(query_logical_processor_count());
	ThreadCountSpinner->setValue(AppState->WhisperThreadCount);
	ThreadCountRow->addWidget(ThreadCountLabel);
	ThreadCountRow->addWidget(ThreadCountSpinner);
	ThreadCountRow->addStretch();
	Layout->addLayout(ThreadCountRow, Row++, 0, 1, 4);

	// Heading
	QLabel *Heading = new QLabel("Keyboard Shortcuts", Dialog);
	QFont HeadingFont = Heading->font();
	HeadingFont.setPointSize(11);
	HeadingFont.setWeight(QFont::Bold);
	Heading->setFont(HeadingFont);
	Layout->addWidget(Heading, Row++, 0, 1, 4);

	// Action selector row
	QPushButton *RecordBtn       = new QPushButton("Record",              Dialog);
	QPushButton *CancelRecordBtn = new QPushButton("Cancel Record",       Dialog);
	QPushButton *StreamBtn       = new QPushButton("Stream",              Dialog);
	QPushButton *LoadModelBtn    = new QPushButton("Load / Unload Model", Dialog);
	RecordBtn->setMinimumHeight(40);
	CancelRecordBtn->setMinimumHeight(40);
	StreamBtn->setMinimumHeight(40);
	LoadModelBtn->setMinimumHeight(40);
	Layout->addWidget(RecordBtn,       Row, 0);
	Layout->addWidget(CancelRecordBtn, Row, 1);
	Layout->addWidget(StreamBtn,       Row, 2);
	Layout->addWidget(LoadModelBtn,    Row, 3);
	Row++;

	// "Current: ..." label
	QLabel *CurrentLabel = new QLabel("", Dialog);
	Layout->addWidget(CurrentLabel, Row++, 0, 1, 4);

	// Shared state - initialize temp values from AppState
	SettingsWindowState S = {};
	S.SelectedAction   = 0;
	S.CurrentLabel     = CurrentLabel;
	S.ActionButtons[0] = RecordBtn;
	S.ActionButtons[1] = CancelRecordBtn;
	S.ActionButtons[2] = StreamBtn;
	S.ActionButtons[3] = LoadModelBtn;
	S.SoundCheckBox      = SoundCheckBox;
	S.CharByCharCheckBox = CharByCharCheckBox;
	S.ThreadCountSpinner = ThreadCountSpinner;

	// Copy current hotkeys to temp storage
	S.TempHotkeys[0] = AppState->RecordHotkey;
	S.TempHotkeys[1] = AppState->CancelRecordHotkey;
	S.TempHotkeys[2] = AppState->StreamHotkey;
	S.TempHotkeys[3] = AppState->LoadModelHotkey;
	S.TempPlayRecordSound         = AppState->PlayRecordSound;
	S.TempUseCharByCharInjection  = AppState->UseCharByCharInjection;
	S.TempWhisperThreadCount      = AppState->WhisperThreadCount;

	SoundCheckBox->setChecked(S.TempPlayRecordSound);
	CharByCharCheckBox->setChecked(S.TempUseCharByCharInjection);

	// Hotkey capture box — filter writes into S.Capture
	QLineEdit *CaptureEdit = nullptr;
	make_hotkey_capture_edit(Dialog, &S.Capture, &CaptureEdit, S.TempHotkeys[0]);
	CaptureEdit->setMinimumHeight(40);
	Layout->addWidget(CaptureEdit, Row++, 0, 1, 4);
	S.CaptureEdit = CaptureEdit;

	// Hint
	QLabel *HintLabel = new QLabel(
		"Select an action above, then click the box and press your desired combination.\n"
		"Modifier-only combos (e.g. Ctrl+Alt) are supported. Escape clears the box.",
		Dialog);
	HintLabel->setWordWrap(true);
	QFont HintFont = HintLabel->font();
	HintFont.setPointSize(9);
	HintFont.setWeight(QFont::Normal);
	HintLabel->setFont(HintFont);
	Layout->addWidget(HintLabel, Row++, 0, 1, 4);

	// Set Hotkey / Save / Cancel buttons
	QPushButton *SetButton   = new QPushButton("Set Hotkey", Dialog);
	QPushButton *SaveButton   = new QPushButton("Save",       Dialog);
	QPushButton *CancelButton = new QPushButton("Cancel",     Dialog);
	SetButton->setMinimumHeight(40);
	SaveButton->setMinimumHeight(40);
	CancelButton->setMinimumHeight(40);
	Layout->addWidget(SetButton,   Row, 0);
	Layout->addWidget(SaveButton,   Row, 1);
	Layout->addWidget(CancelButton, Row, 2);

	// Select Record by default to prime the UI
	settings_select_action(&S, 0);

	// Action selector connections — each button selects its action
	QObject::connect(RecordBtn, &QPushButton::clicked, [&S]()
	{
		settings_select_action(&S, 0);
	});
	QObject::connect(CancelRecordBtn, &QPushButton::clicked, [&S]()
	{
		settings_select_action(&S, 1);
	});
	QObject::connect(StreamBtn, &QPushButton::clicked, [&S]()
	{
		settings_select_action(&S, 2);
	});
	QObject::connect(LoadModelBtn, &QPushButton::clicked, [&S]()
	{
		settings_select_action(&S, 3);
	});

    // TODO(warren): not quite right.
    // click capture edit button -> in temp storage -> hit set hotkey -> save btn -> save to disk and sync to appstate.
    // click capture edit button -> in temp storage -> save btn -> dont save the hotkey in the temp buffer, revert the temp buffer with the one on disk.
	// Set Hotkey — updates temp storage only
	QObject::connect(SetButton, &QPushButton::clicked, [AppState, Dialog, &S]()
	{
		if (!S.Capture.HasCapture)
		{
			QMessageBox::warning(Dialog, "No Hotkey Set",
				"Please press a key combination in the box before clicking Set.");
			return;
		}

		// Store in temp hotkey for the selected action
		S.TempHotkeys[S.SelectedAction] = S.Capture.Captured;

		// Refresh the "Current:" label
		S.CurrentLabel->setText(
			QString("Current: %1").arg(S.Capture.Captured.to_label()));
	});

	QObject::connect(SaveButton, &QPushButton::clicked, [AppState, Dialog, &S]()
	{
		AppState->RecordHotkey       = S.TempHotkeys[0];
		AppState->CancelRecordHotkey = S.TempHotkeys[1];
		AppState->StreamHotkey       = S.TempHotkeys[2];
		AppState->LoadModelHotkey    = S.TempHotkeys[3];

		save_hotkey_setting("record_hotkey",        (int)AppState->RecordHotkey.Modifiers,       (int)AppState->RecordHotkey.Key);
		save_hotkey_setting("cancel_record_hotkey",  (int)AppState->CancelRecordHotkey.Modifiers, (int)AppState->CancelRecordHotkey.Key);
		save_hotkey_setting("stream_hotkey",         (int)AppState->StreamHotkey.Modifiers,       (int)AppState->StreamHotkey.Key);
		save_hotkey_setting("load_model_hotkey",     (int)AppState->LoadModelHotkey.Modifiers,    (int)AppState->LoadModelHotkey.Key);

		AppState->PlayRecordSound = S.SoundCheckBox->isChecked();
		save_bool_setting("play_record_sound", AppState->PlayRecordSound);

		AppState->UseCharByCharInjection = S.CharByCharCheckBox->isChecked();
		save_bool_setting("use_char_by_char_injection", AppState->UseCharByCharInjection);

		update_whisper_thread_count(AppState, S.ThreadCountSpinner->value());

		AppState->RecordButton->setText(record_button_idle_label(AppState));
		AppState->CancelRecordButton->setText(cancel_record_button_idle_label(AppState));
		AppState->StreamButton->setText(stream_button_idle_label(AppState));
		AppState->LoadModelButton->setText(load_model_button_idle_label(AppState));
	});

	QObject::connect(CancelButton, &QPushButton::clicked, [Dialog]()
	{
		Dialog->reject();
	});

	Dialog->exec();
	AppState->IsSettingsDialogOpen = false;
	Dialog->deleteLater();
}

inline
void
init_main_window(GlobalState *AppState)
{
	QWidget *MainWindow = AppState->QtMainWindow;
	std::vector<AudioInputDeviceInfo> *AudioInputDevices = &(AppState->AudioInputDevices);

	QGridLayout *GridLayout = new QGridLayout(MainWindow);
	int Row = 0;

	// Record Button — label reflects current hotkey
	QPushButton *RecordButton = new QPushButton(MainWindow);
	RecordButton->setStyleSheet(BUTTON_STYLE_GREEN);
	RecordButton->setText(QString("Record (%1)").arg(AppState->RecordHotkey.to_label()));
	RecordButton->setMinimumHeight(60);
	GridLayout->addWidget(RecordButton, Row++, 0);
	AppState->RecordButton = RecordButton;

	// Cancel Record Button
	QPushButton *CancelRecordButton = new QPushButton(MainWindow);
	CancelRecordButton->setText(cancel_record_button_idle_label(AppState));
	CancelRecordButton->setMinimumHeight(40);
	CancelRecordButton->setEnabled(false);
	GridLayout->addWidget(CancelRecordButton, Row++, 0);
	AppState->CancelRecordButton = CancelRecordButton;

	// Stream Button
	QPushButton *StreamButton = new QPushButton(MainWindow);
	StreamButton->setStyleSheet(BUTTON_STYLE_GREEN);
	StreamButton->setText(stream_button_idle_label(AppState));
	StreamButton->setMinimumHeight(60);
	GridLayout->addWidget(StreamButton, Row++, 0);
	AppState->StreamButton = StreamButton;

	// Audio Input
	GridLayout->addWidget(new QLabel("Audio Input", MainWindow), Row++, 0);
	QComboBox *AudioInputSelect = new QComboBox(MainWindow);
	for (size_t i = 0; i < AudioInputDevices->size(); i++)
	{
		AudioInputDeviceInfo Device = AudioInputDevices->at(i);
		AudioInputSelect->addItem(QString::fromStdString(Device.Name));
		if (AppState->CurrentAudioDeviceIndex == (int)i)
			AudioInputSelect->setCurrentIndex(i);
		#ifdef DEBUG
			qDebug() << "Initializing device input in select box.";
			qDebug() << "Device.Name: " << Device.Name;
		#endif
	}
	AudioInputSelect->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	GridLayout->addWidget(AudioInputSelect, Row++, 0);
	AppState->AudioInputDropdown = AudioInputSelect;

	// STT Model
	GridLayout->addWidget(new QLabel("STT Model", MainWindow), Row++, 0);
	QComboBox *STTModelSelect = new QComboBox(MainWindow);
	if (!AppState->AnySTTModelAvailable)
	{
		STTModelSelect->addItem("No Models Found");
		STTModelSelect->setEnabled(false);
	}
	else
	{
		for (size_t i = 0; i < AppState->STTModels.size(); i++)
		{
			QString Label = QString::fromStdString(AppState->STTModels.at(i));
			if (!AppState->STTModelAvailable[i]) Label += " [not found]";
			STTModelSelect->addItem(Label);
			if (AppState->CurrentSTTModelIndex == (int)i)
				STTModelSelect->setCurrentIndex(i);
		}
		QStandardItemModel *ComboModel =
			qobject_cast<QStandardItemModel *>(STTModelSelect->model());
		for (size_t i = 0; i < AppState->STTModels.size(); i++)
		{
			if (AppState->STTModelAvailable[i]) continue;
			QStandardItem *Item = ComboModel->item((int)i);
			Item->setFlags(Item->flags() & ~Qt::ItemIsEnabled);
		}
	}
	GridLayout->addWidget(STTModelSelect, Row++, 0);
	AppState->STTModelDropdown = STTModelSelect;

	// Load Model Button
	QPushButton *LoadModelButton = new QPushButton(MainWindow);
	LoadModelButton->setStyleSheet(BUTTON_STYLE_GREY);
	LoadModelButton->setText(load_model_button_idle_label(AppState));
	LoadModelButton->setMinimumHeight(60);
	if (!AppState->AnySTTModelAvailable) LoadModelButton->setEnabled(false);
	GridLayout->addWidget(LoadModelButton, Row++, 0);
	AppState->LoadModelButton = LoadModelButton;

	#ifdef DEBUG
		printf("[ui] Load Model button initialized (grey = not loaded)\n");
	#endif

	// Inference Device
	GridLayout->addWidget(new QLabel("Inference Device", MainWindow), Row++, 0);
	QComboBox *InferenceDeviceSelect = new QComboBox(MainWindow);
	for (size_t i = 0; i < AppState->InferenceDevices.size(); i++)
	{
		InferenceDeviceSelect->addItem(QString::fromStdString(AppState->InferenceDevices.at(i)));
		if (AppState->CurrentInferenceDeviceIndex == (int)i)
			InferenceDeviceSelect->setCurrentIndex(i);
	}
	GridLayout->addWidget(InferenceDeviceSelect, Row++, 0);
	AppState->InferenceDeviceDropdown = InferenceDeviceSelect;

	// Settings Button
	QPushButton *SettingsButton = new QPushButton(MainWindow);
	SettingsButton->setText("Settings");
	SettingsButton->setMinimumHeight(40);
	GridLayout->addWidget(SettingsButton, Row++, 0);
	AppState->SettingsButton = SettingsButton;

	// --- Connections ---
	QObject::connect(RecordButton, &QPushButton::clicked, [AppState]()
	{
		toggle_recording(AppState);
	});

	QObject::connect(CancelRecordButton, &QPushButton::clicked, [AppState]()
	{
		cancel_recording(AppState);
	});

	QObject::connect(StreamButton, &QPushButton::clicked, [AppState]()
	{
		toggle_streaming(AppState);
	});

	QObject::connect(AudioInputSelect, &QComboBox::currentIndexChanged, [AppState](int index)
	{
		update_audio_input_selection(AppState, index);
	});

	QObject::connect(STTModelSelect, &QComboBox::currentIndexChanged, [AppState](int index)
	{
		update_stt_model_selection(AppState, index);
	});

	QObject::connect(LoadModelButton, &QPushButton::clicked, [AppState]()
	{
		toggle_stt_model_load(AppState);
	});

	QObject::connect(InferenceDeviceSelect, &QComboBox::currentIndexChanged, [AppState](int index)
	{
		update_inference_device_selection(AppState, index);
	});

	QObject::connect(SettingsButton, &QPushButton::clicked, [AppState]()
	{
		if (AppState->IsRecording || AppState->IsStreaming) return;
		open_settings_window(AppState);
	});
}

inline
void
init_ui(GlobalState *AppState)
{
	QFont Font("Georgia");
	Font.setPointSize(12);
	Font.setWeight(QFont::Bold);
	AppState->QtApp->setFont(Font);

	AppState->QtMainWindow->setWindowTitle("VoiceTyper");
	AppState->QtMainWindow->resize(WINDOW_DEFAULT_WIDTH, WINDOW_DEFAULT_HEIGHT);

	init_main_window(AppState);
}
