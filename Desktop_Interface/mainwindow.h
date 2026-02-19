#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QActionGroup>
#include <QAction>
#include <QCloseEvent>
#include <QShortcut>
#include <QFont>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QThread>
#include <QPalette>
#include <QDebug>
#include <QBrush>
#include <QGridLayout>

#include <math.h>
#include "qcustomplot.h"

#include "qcustomplot.h"
#include "ui_mainwindow.h"
#include "scoperangeenterdialog.h"
//#include "isobuffer.h"
#include "q_debugstream.h"
#include "pinchcatcher.h"


//The Main Window object.  This has a lot of control information too (keyboard shortcuts etc.)!



namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event);
    void showFileDialog(QString *fileName);
    void openFileDialog(QString *fileName);
private slots:
    //Oscilloscope
    void on_actionGain0_5_triggered();
    void on_actionGain1_triggered();
    void on_actionGain2_triggered();
    void on_actionGain4_triggered();
    void on_actionGain8_triggered();
    void on_actionGain16_triggered();
    void on_actionGain32_triggered();
    void on_actionGain64_triggered();
    void on_actionGainAuto_triggered();
    void on_actionCursor_Stats_triggered(bool checked);
    void on_actionAutomatically_Enable_Cursors_toggled(bool arg1);
    void on_action60FPS_toggled(bool enabled);
    void on_action30FPS_toggled(bool enabled);
    void on_action20FPS_toggled(bool enabled);
    void on_action15FPS_toggled(bool enabled);
    void on_action10FPS_toggled(bool enabled);
    void on_action5FPS_toggled(bool enabled);
    void on_actionSnap_to_Cursors_triggered();
    void on_actionEnter_Manually_triggered();

    void connectDisplaySignals();
    void calibrateStage2();
    void calibrateStage3();


    //Logic Analyzer
    void on_action300_toggled(bool arg1);
    void on_action600_toggled(bool arg1);
    void on_action1200_toggled(bool arg1);
    void on_action2400_toggled(bool arg1);
    void on_action4800_toggled(bool arg1);
    void on_action9600_toggled(bool arg1);
    void on_action14400_toggled(bool arg1);
    void on_action19200_toggled(bool arg1);
    void on_action28800_toggled(bool arg1);
    void on_action38400_toggled(bool arg1);
    void on_action57600_toggled(bool arg1);
    void on_action115200_toggled(bool arg1);

    void on_action300_2_toggled(bool arg1);
    void on_action600_2_toggled(bool arg1);
    void on_action1200_2_toggled(bool arg1);
    void on_action2400_2_toggled(bool arg1);
    void on_action4800_2_toggled(bool arg1);
    void on_action9600_2_toggled(bool arg1);
    void on_action14400_2_toggled(bool arg1);
    void on_action19200_2_toggled(bool arg1);
    void on_action28800_2_toggled(bool arg1);
    void on_action38400_2_toggled(bool arg1);
    void on_action57600_2_toggled(bool arg1);
    void on_action115200_2_toggled(bool arg1);
    void cycleBaudRate_CH1();
    void cycleBaudRateBackwards_CH1();
    void cycleBaudRate_CH2();
    void cycleBaudRateBackwards_CH2();

    //Deprecated/Unsupported
    void timeBaseNeedsChanging(bool positive);
    void on_actionForce_Square_triggered(bool checked);

    //Keyboard Shortcuts
    void arrowUpTriggered();
    void arrowDownTriggered();
    void ctrlArrowUpTriggered();
    void ctrlArrowDownTriggered();
    void cycleDelayLeft();
    void cycleDelayRight();
    void cycleDelayLeft_large();
    void cycleDelayRight_large();
    void enableLabradorDebugging(bool enabled);

    //Power Supply
    void on_actionAuto_Lock_toggled(bool arg1);

    //File/other
    void reinitUsb(void);
    void reinitUsbStage2(void);
    void resetUsbState(void);
    void rSourceIndexChanged(int newSource);
    void multimeterStateChange(bool state);

#ifdef PLATFORM_ANDROID
    //Android Special
    void on_actionOld_Person_Mode_triggered(bool checked);
    void screenRotateEvent(Qt::ScreenOrientation orientation);
    bool eventFilter(QObject *obj, QEvent *event);
    bool gestureFilter(QGestureEvent *event);
    void horiScaleEvent(bool enabled);
    void vertScaleEvent(bool enabled);
    void dpiAutoScaling();
    void on_androidMenuButton_clicked();
#endif

    void on_actionCalibrate_triggered();
    void on_actionLo_bw_triggered();

    void on_actionSingle_ep_msync_triggered();

    void on_actionSingle_ep_async_triggered();

    void on_actionExportImage_triggered();

    void on_actionSnapshot_CH1_triggered();

    void on_actionSnapshot_CH2_triggered();

    void on_actionRecord_CH1_triggered(bool checked);

    void on_actionRecord_CH2_triggered(bool checked);

    void serialEmergencyDisable(int channel);

    void on_actionFirmware_Recovery_triggered();

    void on_actionDAQ_Settings_triggered();

    void fileLimitReached_CH1(void);
    void fileLimitReached_CH2(void);

    void daq_updatedAveraging(int newVal);
    void daq_updatedMaxFileSize(qulonglong newVal);
    void daq_saveButtonPressed();


    void on_actionAbout_triggered();

    void on_actionOpen_DAQ_File_triggered();

    void on_actionQuit_triggered();

    void on_kickstartIsoButton_clicked();

    void on_actionCalibrate_2_triggered();

    void calibrate_psu_stage2();

    void calibrate_psu_stage3();

    void on_actionSerial_triggered(bool checked);

    void on_actionI2C_triggered(bool checked);

    void on_actionShow_Range_Dialog_on_Main_Page_triggered(bool checked);

	void paused(bool enabled);

    void on_actionNone_triggered();

    void on_actionNone_2_triggered();

    void on_actionEven_triggered();

    void on_actionOdd_triggered();

    void on_actionEven_2_triggered();

    void on_actionOdd_2_triggered();

    void on_actionDocumentation_triggered();

    void on_actionPinout_triggered();

    void cursorGroupEnabled(bool enabled);

    void on_actionHide_Widget_Oscilloscope_triggered(bool checked);

    void on_actionHide_Widget_SignalGen_triggered(bool checked);

    void on_actionHide_Widget_Multimeter_triggered(bool checked);

    void on_actionHide_Widget_PowerSupply_triggered(bool checked);

    void on_actionHide_Widget_LogicAnalyzer_triggered(bool checked);

    void on_actionDark_Mode_triggered(bool checked);

    void on_actionShow_Debug_Console_triggered(bool checked);
#ifndef DISABLE_SPECTRUM
    void on_actionFrequency_Spectrum_triggered(bool checked);
    void on_actionFrequency_Response_triggered(bool checked);
    void on_actionEye_Diagram_triggered(bool checked);
#endif

    void on_serialEncodingCheck_CH1_toggled(bool checked);
    void on_txuart_textChanged();

private:
    //Generic Vars
    Ui::MainWindow *ui;
    QWheelEvent *wheelEmu;
    bool forceSquare = false;
    QFile *output375_CH1, *output375_CH2, *output750;
    unsigned char caibrateStage;
    QMessageBox *calibrationMessages;
    double PSU5, PSU10;
    double psu_voltage_calibration_offset;

    int reinitdeviceMode;
    double reinitScopeGain;
    double reinitCurrentPsuVoltage;
    int reinitDigitalPinState;
    
    int defaultAutoTickCount = 9;
    int defaultNumberPrecision = 6;
    QString defaultNumberFormat;

    QSettings *settings;
	bool calibrationCanceled = false;

    QPalette defaultPalette;
    QString defaultStyleName;
    QString prev_text;

    //Generic Functions
    void initialisePlot();
    void labelPsu();
    void menuSetup();
    void initShortcuts();
    void readSettingsFile();
    void setDarkMode(bool dark);
    std::vector<uint8_t> uartEncode(const QString& line, UartParity parity);
    std::vector<uint8_t> resampler(std::vector<uint8_t>, int input_baudRate, int output_baudRate);

    //Shortcut pointers
    QActionGroup *gainGroup;
    QActionGroup *rangeGroupV;
    QActionGroup *rangeGroupI;
    QActionGroup *rangeGroupR;
    QActionGroup *rangeGroupC;
    QActionGroup *uartBaudGroup_CH1;
    QActionGroup *uartBaudGroup_CH2;
    QActionGroup *uartParityGroup_CH1;
    QActionGroup *uartParityGroup_CH2;
    QActionGroup *fpsGroup;
    QActionGroup *connectionTypeGroup;
    QActionGroup *serialProtocolGroup;
    QShortcut *shortcut_cycleBaudRate_CH1;
    QShortcut *shortcut_cycleBaudRateBackwards_CH1;
    QShortcut *shortcut_cycleBaudRate_CH2;
    QShortcut *shortcut_cycleBaudRateBackwards_CH2;
    QShortcut *shortcut_ArrowUp;
    QShortcut *shortcut_ArrowDown;
    QShortcut *shortcut_CtrlArrowUp;
    QShortcut *shortcut_CtrlArrowDown;
    QShortcut *shortcut_w;
    QShortcut *shortcut_ctrlW;
    QShortcut *shortcut_s;
    QShortcut *shortcut_ctrlS;
    QShortcut *shortcut_a;
    QShortcut *shortcut_d;
    QShortcut *shortcut_ArrowLeft;
    QShortcut *shortcut_ArrowRight;
    QShortcut *shortcut_CtrlArrowLeft;
    QShortcut *shortcut_CtrlArrowRight;
    QShortcut *shortcut_snapScopeToCursors;\
    QShortcut *shortcut_manualRange;
    QShortcut *shortcut_snapshot_CH1;
    QShortcut *shortcut_snapshot_CH2;
    QShortcut *shortcut_Debug;
    QShortcut *shortcut_Esc;

#ifndef DISABLE_SPECTRUM
    // Frequency spectrum
    QWidget* spectrumLayoutWidget = nullptr;
    QComboBox* windowingComboBox = nullptr;
    QCheckBox *logHorCheckSpectrum = nullptr;

    // Frequency response
    QWidget* freqRespLayout1Widget = nullptr;
    QWidget* freqRespLayout2Widget = nullptr;
    espoSpinBox* freqRespMinXSpinbox = nullptr;
    espoSpinBox* freqRespMaxXSpinbox = nullptr;
    espoSpinBox* freqRespStepSpinbox = nullptr;
    QComboBox* freqRespTypeComboBox = nullptr;
    QPushButton *freqRespRestartButton = nullptr;

    QCheckBox *logHorCheckResp = nullptr;

    // Eye diagram
    QWidget* eyeDiagramLayoutWidget = nullptr;
    QSpinBox* eyeDiagramNumTracesSpinbox = nullptr;
#endif

    //Duct Tape
    bool dt_AlreadyAskedAboutCalibration = false;
    bool dt_userWantsToCalibrate = false;
    int daq_num_to_average;
    qulonglong daq_max_file_size;

    scopeRangeEnterDialog* scopeRangeSwitch = nullptr;

#ifdef PLATFORM_ANDROID
    //Android Special
    QScreen *screenPtr;
    bool scalingInTimeAxis = false;
#endif
};

#endif // MAINWINDOW_H
