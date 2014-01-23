/**
 ******************************************************************************
 *
 * @file       configtxpidswidget.cpp
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2012.
 * @addtogroup GCSPlugins GCS Plugins
 * @{
 * @addtogroup ConfigPlugin Config Plugin
 * @{
 * @brief The Configuration Gadget used to configure the PipXtreme
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "configpipxtremewidget.h"

#include <coreplugin/generalsettings.h>
#include <oplinksettings.h>
#include <oplinkstatus.h>

ConfigPipXtremeWidget::ConfigPipXtremeWidget(QWidget *parent) : ConfigTaskWidget(parent)
{
    ui = new Ui_OPLinkWidget();
    ui->setupUi(this);

    ExtensionSystem::PluginManager *pluginManager = ExtensionSystem::PluginManager::instance();
    Q_ASSERT(pluginManager);

    Core::Internal::GeneralSettings *settings = pluginManager->getObject<Core::Internal::GeneralSettings>();
    ui->Apply->setVisible(settings->useExpertMode());

    UAVObjectManager *objectmanager = pluginManager->getObject<UAVObjectManager>();
    Q_ASSERT(objectmanager);

    // Connect to the OPLinkStatus object updates
    oplinkStatusObject = OPLinkStatus::GetInstance(objectmanager);
    Q_ASSERT(oplinkStatusObject);
    connect(oplinkStatusObject, SIGNAL(objectUpdated(UAVObject *)), this, SLOT(updateStatus(UAVObject *)));

    // Connect to the OPLinkSettings object updates
    oplinkSettingsObject = OPLinkSettings::GetInstance(objectmanager);
    Q_ASSERT(oplinkSettingsObject);
    connect(oplinkSettingsObject, SIGNAL(objectUpdated(UAVObject *)), this, SLOT(updateSettings(UAVObject *)));

    addApplySaveButtons(ui->Apply, ui->Save);

    // Connect fields
    addWidgetBinding("OPLinkSettings", "MainPort", ui->MainPort);
    addWidgetBinding("OPLinkSettings", "FlexiPort", ui->FlexiPort);
    addWidgetBinding("OPLinkSettings", "VCPPort", ui->VCPPort);
    addWidgetBinding("OPLinkSettings", "ComSpeed", ui->ComSpeed);
    addWidgetBinding("OPLinkSettings", "MaxRFPower", ui->MaxRFTxPower);
    addWidgetBinding("OPLinkSettings", "MinChannel", ui->MinimumChannel);
    addWidgetBinding("OPLinkSettings", "MaxChannel", ui->MaximumChannel);
    addWidgetBinding("OPLinkSettings", "ChannelSet", ui->ChannelSet);
    addWidgetBinding("OPLinkSettings", "CoordID", ui->CoordID);
    addWidgetBinding("OPLinkSettings", "Coordinator", ui->Coordinator);
    addWidgetBinding("OPLinkSettings", "OneWay", ui->OneWayLink);
    addWidgetBinding("OPLinkSettings", "PPMOnly", ui->PPMOnly);
    addWidgetBinding("OPLinkSettings", "PPM", ui->PPM);

    addWidgetBinding("OPLinkStatus", "DeviceID", ui->DeviceID);
    addWidgetBinding("OPLinkStatus", "RxGood", ui->Good);
    addWidgetBinding("OPLinkStatus", "RxCorrected", ui->Corrected);
    addWidgetBinding("OPLinkStatus", "RxErrors", ui->Errors);
    addWidgetBinding("OPLinkStatus", "RxMissed", ui->Missed);
    addWidgetBinding("OPLinkStatus", "RxFailure", ui->RxFailure);
    addWidgetBinding("OPLinkStatus", "UAVTalkErrors", ui->UAVTalkErrors);
    addWidgetBinding("OPLinkStatus", "TxDropped", ui->Dropped);
    addWidgetBinding("OPLinkStatus", "TxResent", ui->Resent);
    addWidgetBinding("OPLinkStatus", "TxFailure", ui->TxFailure);
    addWidgetBinding("OPLinkStatus", "Resets", ui->Resets);
    addWidgetBinding("OPLinkStatus", "Timeouts", ui->Timeouts);
    addWidgetBinding("OPLinkStatus", "RSSI", ui->RSSI);
    addWidgetBinding("OPLinkStatus", "HeapRemaining", ui->FreeHeap);
    addWidgetBinding("OPLinkStatus", "LinkQuality", ui->LinkQuality);
    addWidgetBinding("OPLinkStatus", "RXSeq", ui->RXSeq);
    addWidgetBinding("OPLinkStatus", "TXSeq", ui->TXSeq);
    addWidgetBinding("OPLinkStatus", "RXRate", ui->RXRate);
    addWidgetBinding("OPLinkStatus", "TXRate", ui->TXRate);

    // Connect the bind buttons
    connect(ui->Bind1, SIGNAL(clicked()), this, SLOT(bind1()));
    connect(ui->Bind2, SIGNAL(clicked()), this, SLOT(bind2()));
    connect(ui->Bind3, SIGNAL(clicked()), this, SLOT(bind3()));
    connect(ui->Bind4, SIGNAL(clicked()), this, SLOT(bind4()));

    // Connect the selection changed signals.
    connect(ui->PPMOnly, SIGNAL(toggled(bool)), this, SLOT(ppmOnlyToggled(bool)));
    connect(ui->ComSpeed, SIGNAL(currentIndexChanged(int)), this, SLOT(comSpeedChanged(int)));

    ppmOnlyToggled(ui->PPMOnly->isChecked());

    // Request and update of the setting object.
    settingsUpdated = false;
    autoLoadWidgets();
    disableMouseWheelEvents();
}

ConfigPipXtremeWidget::~ConfigPipXtremeWidget()
{}

void ConfigPipXtremeWidget::updateStatus(UAVObject *object)
{
    // Request and update of the setting object if we haven't received it yet.
    if (!settingsUpdated) {
        oplinkSettingsObject->requestUpdate();
    }

    // Update the link state
    UAVObjectField *linkState = object->getField("LinkState");
    ui->LinkState->setText(linkState->getValue().toString());

    // Update the detected devices.
    UAVObjectField *pairIdField = object->getField("PairIDs");
    quint32 pairid = pairIdField->getValue(0).toUInt();
    ui->PairID1->setText(QString::number(pairid, 16).toUpper());
    ui->PairID1->setEnabled(false);
    ui->Bind1->setEnabled(pairid);
    pairid = pairIdField->getValue(1).toUInt();
    ui->PairID2->setText(QString::number(pairIdField->getValue(1).toUInt(), 16).toUpper());
    ui->PairID2->setEnabled(false);
    ui->Bind2->setEnabled(pairid);
    pairid = pairIdField->getValue(2).toUInt();
    ui->PairID3->setText(QString::number(pairIdField->getValue(2).toUInt(), 16).toUpper());
    ui->PairID3->setEnabled(false);
    ui->Bind3->setEnabled(pairid);
    pairid = pairIdField->getValue(3).toUInt();
    ui->PairID4->setText(QString::number(pairIdField->getValue(3).toUInt(), 16).toUpper());
    ui->PairID4->setEnabled(false);
    ui->Bind4->setEnabled(pairid);

    if(linkState->getValue() == linkState->getOptions().at(OPLinkStatus::LINKSTATE_DISABLED) ||
            linkState->getValue() == linkState->getOptions().at(OPLinkStatus::LINKSTATE_DISCONNECTED) ) {
        ui->PairSignalStrengthBar1->setValue(ui->PairSignalStrengthBar1->minimum());
        ui->PairSignalStrengthBar2->setValue(ui->PairSignalStrengthBar2->minimum());
        ui->PairSignalStrengthBar3->setValue(ui->PairSignalStrengthBar3->minimum());
        ui->PairSignalStrengthBar4->setValue(ui->PairSignalStrengthBar4->minimum());
    } else {
        UAVObjectField *pairRssiField = object->getField("PairSignalStrengths");
        ui->PairSignalStrengthBar1->setValue(pairRssiField->getValue(0).toInt());
        ui->PairSignalStrengthBar2->setValue(pairRssiField->getValue(1).toInt());
        ui->PairSignalStrengthBar3->setValue(pairRssiField->getValue(2).toInt());
        ui->PairSignalStrengthBar4->setValue(pairRssiField->getValue(3).toInt());
    }
    ui->PairSignalStrengthLabel1->setText(QString("%1dB").arg(ui->PairSignalStrengthBar1->value()));
    ui->PairSignalStrengthLabel2->setText(QString("%1dB").arg(ui->PairSignalStrengthBar2->value()));
    ui->PairSignalStrengthLabel3->setText(QString("%1dB").arg(ui->PairSignalStrengthBar3->value()));
    ui->PairSignalStrengthLabel4->setText(QString("%1dB").arg(ui->PairSignalStrengthBar4->value()));

    // Update the Description field
    // TODO use  UAVObjectUtilManager::descriptionToStructure()
    UAVObjectField *descField = object->getField("Description");
    if (descField->getValue(0) != QChar(255)) {
        /*
         * This looks like a binary with a description at the end:
         *   4 bytes: header: "OpFw".
         *   4 bytes: GIT commit tag (short version of SHA1).
         *   4 bytes: Unix timestamp of compile time.
         *   2 bytes: target platform. Should follow same rule as BOARD_TYPE and BOARD_REVISION in board define files.
         *  26 bytes: commit tag if it is there, otherwise branch name. '-dirty' may be added if needed. Zero-padded.
         *  20 bytes: SHA1 sum of the firmware.
         *  20 bytes: SHA1 sum of the uavo definitions.
         *  20 bytes: free for now.
         */
        char buf[OPLinkStatus::DESCRIPTION_NUMELEM];
        for (unsigned int i = 0; i < 26; ++i) {
            buf[i] = descField->getValue(i + 14).toChar().toLatin1();
        }
        buf[26] = '\0';
        QString descstr(buf);
        quint32 gitDate = descField->getValue(11).toChar().toLatin1() & 0xFF;
        for (int i = 1; i < 4; i++) {
            gitDate  = gitDate << 8;
            gitDate += descField->getValue(11 - i).toChar().toLatin1() & 0xFF;
        }
        QString date = QDateTime::fromTime_t(gitDate).toUTC().toString("yyyy-MM-dd HH:mm");
        ui->FirmwareVersion->setText(descstr + " " + date);
    } else {
        ui->FirmwareVersion->setText(tr("Unknown"));
    }

    // Update the serial number field
    UAVObjectField *serialField = object->getField("CPUSerial");
    char buf[OPLinkStatus::CPUSERIAL_NUMELEM * 2 + 1];
    for (unsigned int i = 0; i < OPLinkStatus::CPUSERIAL_NUMELEM; ++i) {
        unsigned char val = serialField->getValue(i).toUInt() >> 4;
        buf[i * 2]     = ((val < 10) ? '0' : '7') + val;
        val = serialField->getValue(i).toUInt() & 0xf;
        buf[i * 2 + 1] = ((val < 10) ? '0' : '7') + val;
    }
    buf[OPLinkStatus::CPUSERIAL_NUMELEM * 2] = '\0';
    ui->SerialNumber->setText(buf);
}

void ConfigPipXtremeWidget::updateSettings(UAVObject *object)
{
    Q_UNUSED(object);

    if (!settingsUpdated) {
        settingsUpdated = true;

        // Enable components based on the board type connected.
        UAVObjectField *board_type_field = oplinkStatusObject->getField("BoardType");
        switch (board_type_field->getValue().toInt()) {
        case 0x09: // Revolution
            ui->MainPort->setVisible(false);
            ui->MainPortLabel->setVisible(false);
            ui->FlexiPort->setVisible(false);
            ui->FlexiPortLabel->setVisible(false);
            ui->VCPPort->setVisible(false);
            ui->VCPPortLabel->setVisible(false);
            ui->FlexiIOPort->setVisible(false);
            ui->FlexiIOPortLabel->setVisible(false);
            ui->PPM->setVisible(true);
            break;
        case 0x03: // OPLinkMini
            ui->MainPort->setVisible(true);
            ui->MainPortLabel->setVisible(true);
            ui->FlexiPort->setVisible(true);
            ui->FlexiPortLabel->setVisible(true);
            ui->VCPPort->setVisible(true);
            ui->VCPPortLabel->setVisible(true);
            ui->FlexiIOPort->setVisible(false);
            ui->FlexiIOPortLabel->setVisible(false);
            ui->PPM->setVisible(false);
            break;
        case 0x0A:
            ui->MainPort->setVisible(true);
            ui->MainPortLabel->setVisible(true);
            ui->FlexiPort->setVisible(true);
            ui->FlexiPortLabel->setVisible(true);
            ui->VCPPort->setVisible(true);
            ui->VCPPortLabel->setVisible(true);
            ui->FlexiIOPort->setVisible(true);
            ui->FlexiIOPortLabel->setVisible(true);
            ui->PPM->setVisible(false);
            break;
        default:
            // This shouldn't happen.
            break;
        }

        // Enable the push buttons.
        enableControls(true);
    }
}

void ConfigPipXtremeWidget::disconnected()
{
    if (settingsUpdated) {
        settingsUpdated = false;

        // Enable the push buttons.
        enableControls(false);
    }
}

void ConfigPipXtremeWidget::SetPairID(QLineEdit *pairIdWidget)
{
    // Get the pair ID out of the selection widget
    quint32 pairID = 0;
    bool okay;

    pairID = pairIdWidget->text().toUInt(&okay, 16);

    // Store the ID in the coord ID field.
    ui->CoordID->setText(QString::number(pairID, 16).toUpper());
}

void ConfigPipXtremeWidget::bind1()
{
    SetPairID(ui->PairID1);
}

void ConfigPipXtremeWidget::bind2()
{
    SetPairID(ui->PairID2);
}

void ConfigPipXtremeWidget::bind3()
{
    SetPairID(ui->PairID3);
}

void ConfigPipXtremeWidget::bind4()
{
    SetPairID(ui->PairID4);
}

void ConfigPipXtremeWidget::ppmOnlyToggled(bool on)
{
    if (on) {
        ui->PPM->setEnabled(false);
        ui->OneWayLink->setEnabled(false);
        ui->ComSpeed->setEnabled(false);
    } else {
        ui->PPM->setEnabled(true);
        ui->OneWayLink->setEnabled(true);
        ui->ComSpeed->setEnabled(true);
        // Change the comspeed from 4800 of PPM only is turned off.
        if (ui->ComSpeed->currentIndex() == OPLinkSettings::COMSPEED_4800) {
            ui->ComSpeed->setCurrentIndex(OPLinkSettings::COMSPEED_9600);
        }
    }
}

void ConfigPipXtremeWidget::comSpeedChanged(int index)
{
    qDebug() << "comSpeedChanged: " << index;
    switch (index) {
    case OPLinkSettings::COMSPEED_4800:
        ui->PPMOnly->setChecked(true);
        break;
    default:
        ui->PPMOnly->setChecked(false);
        break;
    }
}
