#include "scoperangeenterdialog.h"
#include "ui_scoperangeenterdialog.h"

scopeRangeEnterDialog::scopeRangeEnterDialog(QWidget *parent, bool buttonVisible, double yTop, double yBot, double window, double delay) :
    QDialog(parent),
    ui(new Ui::scopeRangeEnterDialog)
{
    ui->setupUi(this);

    ui->vMaxBox->setMinimum(yBot);
    ui->vMinBox->setMaximum(yTop);

    ui->vMaxBox->setValue(yTop);
    ui->vMinBox->setValue(yBot);
    ui->timeWindowBox->setValue(window);
    ui->delayBox->setValue(delay);
    ui->buttonBox->setVisible(buttonVisible);

    for (espoSpinBox* spinBox : {ui->vMaxBox, ui->vMinBox, ui->timeWindowBox, ui->delayBox})
	{
		spinBox->changeStepping(spinBox->value());
        connect(spinBox, SIGNAL(valueChanged(double)), spinBox, SLOT(changeStepping(double)));
	}
}

scopeRangeEnterDialog::~scopeRangeEnterDialog()
{
    delete ui;
}

void scopeRangeEnterDialog::toUpdateYTop(double val){
    qDebug() << val;

    if (yTop != val)
    {
        yTop = val;
        yTopUpdated(val);
    }
}

void scopeRangeEnterDialog::toUpdateYBot(double val){
    qDebug() << val;

    if (yBot != val)
    {
        yBot = val;
        yBotUpdated(val);
    }
}

void scopeRangeEnterDialog::toUpdateWindow(double val){
    qDebug() << val;

    if (timeWindow != val)
    {
        ui->delayBox->setMax(((double)MAX_WINDOW_SIZE) - ui->timeWindowBox->value());
        qDebug() << "delayBox updating to" << ui->delayBox->maximum();
        timeWindow = val;
        windowUpdated(val);
    }
}

void scopeRangeEnterDialog::toUpdateDelay(double val){
    qDebug() << val;

    if (delay != val)
    {
        ui->timeWindowBox->setMax(((double)MAX_WINDOW_SIZE) - ui->delayBox->value());
        qDebug() << "timeWindowBox updating max to" << ui->timeWindowBox->maximum();
        delay = val;
        delayUpdated(val);
    }
}

void scopeRangeEnterDialog::yTopChanged(double val)
{
    ui->vMaxBox->setValue(val);
}

void scopeRangeEnterDialog::yBotChanged(double val)
{
    ui->vMinBox->setValue(val);
}

void scopeRangeEnterDialog::windowChanged(double val)
{
    ui->timeWindowBox->setValue(val);
}

void scopeRangeEnterDialog::delayChanged(double val)
{
    ui->delayBox->setValue(val);
}

