// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

///
/// \file   RawQcTask.cxx
/// \author Dmitri Peresunko
/// \author Dmitry Blau
///

#include <TCanvas.h>
#include <TH2.h>
#include <TMath.h>
#include <TSpectrum.h>
#include <cfloat>

#include "QualityControl/QcInfoLogger.h"
#include "PHOS/RawQcTask.h"
#include "Headers/RAWDataHeader.h"
#include "PHOSReconstruction/RawReaderError.h"
#include "PHOSBase/Geometry.h"
#include "PHOSBase/Mapping.h"
#include "Framework/InputRecord.h"
#include "CommonUtils/NameConf.h"
#include "CCDB/BasicCCDBManager.h"

namespace o2::quality_control_modules::phos
{

RawQcTask::~RawQcTask()
{

  for (int i = kNhist1D; i--;) {
    if (mHist1D[i]) {
      mHist1D[i]->Delete();
      mHist1D[i] = nullptr;
    }
  }
  for (int i = kNhist2D; i--;) {
    if (mHist2D[i]) {
      mHist2D[i]->Delete();
      mHist2D[i] = nullptr;
    }
  }
}
void RawQcTask::initialize(o2::framework::InitContext& /*ctx*/)
{
  using infoCONTEXT = AliceO2::InfoLogger::InfoLoggerContext;
  infoCONTEXT context;
  context.setField(infoCONTEXT::FieldName::Facility, "QC");
  context.setField(infoCONTEXT::FieldName::System, "QC");
  context.setField(infoCONTEXT::FieldName::Detector, "PHS");
  QcInfoLogger::GetInfoLogger().setContext(context);
  ILOG(Info, Support) << "initialize RawQcTask" << AliceO2::InfoLogger::InfoLogger::endm;

  // this is how to get access to custom parameters defined in the config file at qc.tasks.<task_name>.taskParameters
  if (auto param = mCustomParameters.find("pedestal"); param != mCustomParameters.end()) {
    ILOG(Info, Support) << "Working in pedestal mode " << AliceO2::InfoLogger::InfoLogger::endm;
    if (param->second.find("on") != std::string::npos) {
      mMode = 1;
    }
  }
  if (auto param = mCustomParameters.find("LED"); param != mCustomParameters.end()) {
    ILOG(Info, Support) << "Working in LED mode " << AliceO2::InfoLogger::InfoLogger::endm;
    if (param->second.find("on") != std::string::npos) {
      mMode = 2;
    }
  }
  if (auto param = mCustomParameters.find("physics"); param != mCustomParameters.end()) {
    ILOG(Info, Support) << "Working in physics mode " << AliceO2::InfoLogger::InfoLogger::endm;
    if (param->second.find("on") != std::string::npos) {
      mMode = 0;
    }
  }
  if (auto param = mCustomParameters.find("chi2"); param != mCustomParameters.end()) {
    ILOG(Info, Support) << "Scan chi2 distributions " << AliceO2::InfoLogger::InfoLogger::endm;
    if (param->second.find("on") != std::string::npos) {
      mCheckChi2 = true;
    }
  }

  InitHistograms();
}

void RawQcTask::InitHistograms()
{

  // First init general histograms for any mode

  // Statistics histograms
  mHist2D[kErrorNumber] = new TH2F("NumberOfErrors", "Number of hardware errors", 32, 0, 32, 15, 0, 15.); // xaxis: FEE card number + 2 for TRU and global errors
  mHist2D[kErrorNumber]->GetXaxis()->SetTitle("FEE card");
  mHist2D[kErrorNumber]->GetYaxis()->SetTitle("DDL");
  mHist2D[kErrorNumber]->SetDrawOption("colz");
  mHist2D[kErrorNumber]->SetStats(0);
  getObjectsManager()->startPublishing(mHist2D[kErrorNumber]);

  mHist2D[kErrorType] = new TH2F("ErrorTypePerDDL", "ErrorTypePerDDL", 32, 0, 32, 15, 0, 15.);
  mHist2D[kErrorType]->GetXaxis()->SetTitle("FEE card");
  mHist2D[kErrorType]->GetYaxis()->SetTitle("DDL");
  mHist2D[kErrorType]->SetDrawOption("colz");
  mHist2D[kErrorType]->SetStats(0);
  getObjectsManager()->startPublishing(mHist2D[kErrorType]);

  mHist1D[kBadMapSummary] = new TH1F("BadMapSummary", "Number of bad channels", 4, 1., 5.); // xaxis: FEE card number + 2 for TRU and global errors
  mHist1D[kBadMapSummary]->GetXaxis()->SetTitle("module");
  mHist1D[kBadMapSummary]->GetYaxis()->SetTitle("N bad channels");
  mHist1D[kBadMapSummary]->SetDrawOption("h");
  mHist1D[kBadMapSummary]->SetStats(0);
  getObjectsManager()->startPublishing(mHist1D[kBadMapSummary]);

  if (mCheckChi2) {
    for (Int_t mod = 0; mod < 4; mod++) {
      if (!mHist2D[kChi2M1 + mod]) {
        mHist2D[kChi2M1 + mod] = new TH2F(Form("Chi2M%d", mod + 1), Form("sample fit #chi2/NDF, mod %d", mod + 1), 64, 0., 64., 56, 0., 56.);
        mHist2D[kChi2M1 + mod]->GetXaxis()->SetNdivisions(508, kFALSE);
        mHist2D[kChi2M1 + mod]->GetYaxis()->SetNdivisions(514, kFALSE);
        mHist2D[kChi2M1 + mod]->GetXaxis()->SetTitle("x, cells");
        mHist2D[kChi2M1 + mod]->GetYaxis()->SetTitle("z, cells");
        mHist2D[kChi2M1 + mod]->SetStats(0);
        mHist2D[kChi2M1 + mod]->SetMinimum(0);
        getObjectsManager()->startPublishing(mHist2D[kChi2M1 + mod]);

        mHist2D[kChi2NormM1 + mod] = new TH2F(Form("Chi2NormM%d", mod + 1), Form("sample fit #chi2/NDF normalization, mod %d", mod + 1), 64, 0., 64., 56, 0., 56.);
        mHist2D[kChi2NormM1 + mod]->GetXaxis()->SetNdivisions(508, kFALSE);
        mHist2D[kChi2NormM1 + mod]->GetYaxis()->SetNdivisions(514, kFALSE);
        mHist2D[kChi2NormM1 + mod]->GetXaxis()->SetTitle("x, cells");
        mHist2D[kChi2NormM1 + mod]->GetYaxis()->SetTitle("z, cells");
        mHist2D[kChi2NormM1 + mod]->SetStats(0);
        mHist2D[kChi2NormM1 + mod]->SetMinimum(0);
        // getObjectsManager()->startPublishing(mHist2D[kChi2NormM1 + mod]);
      } else {
        mHist2D[kChi2M1 + mod]->Reset();
        mHist2D[kChi2NormM1 + mod]->Reset();
      }
    }
  }

  if (mMode == 0) { // Physics
    CreatePhysicsHistograms();
  }
  if (mMode == 1) { // Pedestals
    CreatePedestalHistograms();
  }
  if (mMode == 2) { // LED
    CreateLEDHistograms();
  }
}

void RawQcTask::startOfActivity(Activity& /*activity*/)
{
  ILOG(Info, Support) << "startOfActivity" << AliceO2::InfoLogger::InfoLogger::endm;
  reset();
}

void RawQcTask::startOfCycle()
{
  ILOG(Info, Support) << "startOfCycle" << AliceO2::InfoLogger::InfoLogger::endm;
  if (mCheckChi2) {
    if (mFinalized) { // mean already calculated
      for (Int_t mod = 0; mod < 4; mod++) {
        if (mHist2D[kChi2M1 + mod]) {
          mHist2D[kChi2M1 + mod]->Multiply(mHist2D[kChi2NormM1 + mod]);
        }
      }
      if (mMode != 1) { // for mode 1 another check below
        mFinalized = false;
      }
    }
  }

  if (mMode == 1) {   // Pedestals
    if (mFinalized) { // means were already calculated
      for (Int_t mod = 0; mod < 4; mod++) {
        if (mHist2D[kHGmeanM1 + mod]) {
          mHist2D[kHGmeanM1 + mod]->Multiply(mHist2D[kHGoccupM1 + mod]);
          mHist2D[kHGrmsM1 + mod]->Multiply(mHist2D[kHGoccupM1 + mod]);
        }
        if (mHist2D[kLGmeanM1 + mod]) {
          mHist2D[kLGmeanM1 + mod]->Multiply(mHist2D[kLGoccupM1 + mod]);
          mHist2D[kLGrmsM1 + mod]->Multiply(mHist2D[kLGoccupM1 + mod]);
        }
      }
      mFinalized = false;
    }
  }
}

void RawQcTask::monitorData(o2::framework::ProcessingContext& ctx)
{
  // In this function you can access data inputs specified in the JSON config file, for example:
  //   "query": "random:ITS/RAWDATA/0"
  // which is correspondingly <binding>:<dataOrigin>/<dataDescription>/<subSpecification
  // One can also access conditions from CCDB, via separate API (see point 3)

  // Use Framework/DataRefUtils.h or Framework/InputRecord.h to access and unpack inputs (both are documented)
  // One can find additional examples at:
  // https://github.com/AliceO2Group/AliceO2/blob/dev/Framework/Core/README.md#using-inputs---the-inputrecord-api

  auto hwerrors = ctx.inputs().get<std::vector<o2::phos::RawReaderError>>("rawerr");
  for (auto e : hwerrors) {
    int ibin = mHist2D[kErrorNumber]->Fill(float(e.getFEC()), float(e.getDDL()));
    int cont = mHist2D[kErrorType]->GetBinContent(ibin);
    cont |= (1 << e.getError());
    ILOG(Info, Support) << "[" << int(e.getFEC()) << "," << int(e.getDDL()) << "ERROR:" << cont << " e.getErr=" << int(e.getError()) << AliceO2::InfoLogger::InfoLogger::endm;
    mHist2D[kErrorType]->SetBinContent(ibin, cont);
  }
  // Bad Map
  //  Read current bad map if not read yet
  if (mInitBadMap) {
    mInitBadMap = false;
    ILOG(Info, Support) << "Getting bad map" << AliceO2::InfoLogger::InfoLogger::endm;
    loadCcdb();
    std::map<std::string, std::string> metadata;
    mBadMap = retrieveConditionAny<o2::phos::BadChannelsMap>("PHS/Calib/BadMap", metadata);
    if (!mBadMap) {
      ILOG(Error, Support) << "Can not get bad map" << AliceO2::InfoLogger::InfoLogger::endm;
      mHist1D[kBadMapSummary]->Reset();
    } else {
      unsigned short nbm[4] = { 0 };
      for (short absId = 1973; absId <= o2::phos::Mapping::NCHANNELS; absId++) {
        if (!mBadMap->isChannelGood(absId)) {
          nbm[(absId - 1) / 3584]++;
        }
      }
      for (int mod = 0; mod < 4; mod++) {
        mHist1D[kBadMapSummary]->SetBinContent(mod + 1, nbm[mod]);
      }
      ILOG(Info, Support) << "Bad channels:[" << nbm[0] << "," << nbm[1] << "," << nbm[2] << "," << nbm[3] << "]" << AliceO2::InfoLogger::InfoLogger::endm;
    }
  }

  // Chi2: not hardware errors but unusual/correpted sample
  if (mCheckChi2) {
    // vector contains subsequent pairs (address,chi2)
    auto chi2list = ctx.inputs().get<std::vector<short>>("fitquality");
    auto it = chi2list.begin();
    while (it != chi2list.end()) {
      short address = *it;
      it++;
      bool caloFlag = address & 1 << 14;
      address &= ~(1 << 14); // remove HG/LG bit 14
      float chi = 0.2 * (*it);
      it++;
      char relid[3];
      o2::phos::Geometry::absToRelNumbering(address, relid);
      mHist2D[kChi2M1 + relid[0] - 1]->Fill(relid[1] - 0.5, relid[2] - 0.5, chi);
      mHist2D[kChi2NormM1 + relid[0] - 1]->Fill(relid[1] - 0.5, relid[2] - 0.5);
    }
  }

  // Cells
  auto cells = ctx.inputs().get<gsl::span<o2::phos::Cell>>("cells");
  auto cellsTR = ctx.inputs().get<gsl::span<o2::phos::TriggerRecord>>("cellstr");

  if (mMode == 0) { // Physics
    FillPhysicsHistograms(cells, cellsTR);
  }
  if (mMode == 1) { // Pedestals
    FillPedestalHistograms(cells, cellsTR);
  }
  if (mMode == 2) { // LED
    FillLEDHistograms(cells, cellsTR);
  }
} // function monitor data

void RawQcTask::endOfCycle()
{
  if (mCheckChi2) {
    if (!mFinalized) { // not already calculated
      for (Int_t mod = 0; mod < 4; mod++) {
        if (mHist2D[kChi2M1 + mod]) {
          mHist2D[kChi2M1 + mod]->Divide(mHist2D[kChi2NormM1 + mod]);
        }
      }
    }
  }

  if (mMode == 1) {   // Pedestals
    if (mFinalized) { // means were already calculated
      return;
    }
    for (Int_t mod = 0; mod < 4; mod++) {
      if (mHist2D[kHGmeanM1 + mod]) {
        mHist2D[kHGmeanM1 + mod]->Divide(mHist2D[kHGoccupM1 + mod]);
        mHist2D[kHGrmsM1 + mod]->Divide(mHist2D[kHGoccupM1 + mod]);
        mHist1D[kHGmeanSummaryM1 + mod]->Reset();
        mHist1D[kHGrmsSummaryM1 + mod]->Reset();
        double occMin = 1.e+9;
        double occMax = 0.;
        for (int iz = 1; iz <= 64; iz++) {
          for (int ix = 1; ix <= 56; ix++) {
            float a = mHist2D[kHGmeanM1 + mod]->GetBinContent(iz, ix);
            if (a > 0) {
              mHist1D[kHGmeanSummaryM1 + mod]->Fill(a);
            }
            a = mHist2D[kHGrmsM1 + mod]->GetBinContent(iz, ix);
            if (a > 0) {
              mHist1D[kHGrmsSummaryM1 + mod]->Fill(a);
            }
            a = mHist2D[kHGoccupM1 + mod]->GetBinContent(iz, ix);
            if (a > 0) {
              if (a < occMin)
                occMin = a;
              if (a > occMax)
                occMax = a;
            }
          }
        }
        mHist2D[kHGoccupM1 + mod]->SetMinimum(occMin);
        mHist2D[kHGoccupM1 + mod]->SetMaximum(occMax);
      }
      if (mHist2D[kLGmeanM1 + mod]) {
        mHist2D[kLGmeanM1 + mod]->Divide(mHist2D[kLGoccupM1 + mod]);
        mHist2D[kLGrmsM1 + mod]->Divide(mHist2D[kLGoccupM1 + mod]);
        mHist1D[kLGmeanSummaryM1 + mod]->Reset();
        mHist1D[kLGrmsSummaryM1 + mod]->Reset();
        double occMin = 1.e+9;
        double occMax = 0.;
        for (int iz = 1; iz <= 64; iz++) {
          for (int ix = 1; ix <= 56; ix++) {
            float a = mHist2D[kLGmeanM1 + mod]->GetBinContent(iz, ix);
            if (a > 0) {
              mHist1D[kLGmeanSummaryM1 + mod]->Fill(a);
            }
            a = mHist2D[kLGrmsM1 + mod]->GetBinContent(iz, ix);
            if (a > 0) {
              mHist1D[kLGrmsSummaryM1 + mod]->Fill(a);
            }
            a = mHist2D[kLGoccupM1 + mod]->GetBinContent(iz, ix);
            if (a > 0) {
              if (a < occMin)
                occMin = a;
              if (a > occMax)
                occMax = a;
            }
          }
        }
        mHist2D[kLGoccupM1 + mod]->SetMinimum(occMin);
        mHist2D[kLGoccupM1 + mod]->SetMaximum(occMax);
      }
    }
    mFinalized = true;
  }
  //==========LED===========
  if (mMode == 2) { // LED
    ILOG(Info, Support) << " Caclulating number of peaks" << AliceO2::InfoLogger::InfoLogger::endm;
    for (unsigned int absId = 1793; absId <= o2::phos::Mapping::NCHANNELS; absId++) {
      int npeaks = mSpSearcher->Search(&(mSpectra[absId - 1793]), 2, "goff", 0.1);
      char relid[3];
      o2::phos::Geometry::absToRelNumbering(absId, relid);
      short mod = relid[0] - 1;
      int ibin = mHist2D[kLEDNpeaksM1 + mod]->FindBin(relid[1] - 0.5, relid[2] - 0.5);
      mHist2D[kLEDNpeaksM1 + mod]->SetBinContent(ibin, npeaks);
    }
    ILOG(Info, Support) << " Caclulating number of peaks done" << AliceO2::InfoLogger::InfoLogger::endm;
  }
}

void RawQcTask::endOfActivity(Activity& /*activity*/)
{
  endOfCycle();
  ILOG(Info, Support) << "endOfActivity" << AliceO2::InfoLogger::InfoLogger::endm;
}

void RawQcTask::reset()
{
  // clean all the monitor objects here
  mFinalized = false;

  ILOG(Info, Support) << "Resetting the histogram" << AliceO2::InfoLogger::InfoLogger::endm;
  for (int i = kNhist1D; i--;) {
    if (mHist1D[i]) {
      mHist1D[i]->Reset();
    }
  }
  for (int i = kNhist2D; i--;) {
    if (mHist2D[i]) {
      mHist2D[i]->Reset();
    }
  }
}
void RawQcTask::FillLEDHistograms(const gsl::span<const o2::phos::Cell>& cells, const gsl::span<const o2::phos::TriggerRecord>& cellsTR)
{
  FillPhysicsHistograms(cells, cellsTR);

  // Fill intermediate histograms
  for (const auto& tr : cellsTR) {
    int firstCellInEvent = tr.getFirstEntry();
    int lastCellInEvent = firstCellInEvent + tr.getNumberOfObjects();
    for (int i = firstCellInEvent; i < lastCellInEvent; i++) {
      const o2::phos::Cell c = cells[i];
      if (c.getHighGain()) {
        mSpectra[c.getAbsId() - 1793].Fill(c.getEnergy());
      }
    }
  }
}
void RawQcTask::FillPhysicsHistograms(const gsl::span<const o2::phos::Cell>& cells, const gsl::span<const o2::phos::TriggerRecord>& cellsTR)
{
  for (const auto& tr : cellsTR) {
    int firstCellInEvent = tr.getFirstEntry();
    int lastCellInEvent = firstCellInEvent + tr.getNumberOfObjects();
    for (int i = firstCellInEvent; i < lastCellInEvent; i++) {
      const o2::phos::Cell c = cells[i];
      // short cell, float amplitude, float time, int label
      short address = c.getAbsId();
      float e = c.getEnergy();
      if (!c.getHighGain()) {
        e *= 16; // scale LowGain cells
      }
      if (e > kOcccupancyTh) {
        // Converts the absolute numbering into the following array
        //  relid[0] = PHOS Module number 1,...4:module
        //  relid[1] = Row number inside a PHOS module (Phi coordinate)
        //  relid[2] = Column number inside a PHOS module (Z coordinate)
        char relid[3];
        o2::phos::Geometry::absToRelNumbering(address, relid);
        short mod = relid[0] - 1;
        int ibin = mHist2D[kCellOccupM1 + mod]->FindBin(relid[1] - 0.5, relid[2] - 0.5);
        float emean = e;
        float n = mHist2D[kCellOccupM1 + mod]->GetBinContent(ibin);
        if (n > 0) {
          emean = (e + mHist2D[kCellEM1 + mod]->GetBinContent(ibin) * n) / (n + 1);
        }
        mHist2D[kCellEM1 + mod]->SetBinContent(ibin, emean);
        mHist2D[kCellOccupM1 + mod]->AddBinContent(ibin);
        mHist2D[kTimeEM1 + mod]->Fill(e, c.getTime());
        mHist1D[kCellSpM1 + mod]->Fill(e);
      }
    }
  }
}

void RawQcTask::FillPedestalHistograms(const gsl::span<const o2::phos::Cell>& cells, const gsl::span<const o2::phos::TriggerRecord>& cellsTR)
{
  if (mFinalized) {
    for (Int_t mod = 0; mod < 4; mod++) {
      mHist2D[kHGmeanM1 + mod]->Multiply(mHist2D[kHGoccupM1 + mod]);
      mHist2D[kHGrmsM1 + mod]->Multiply(mHist2D[kHGoccupM1 + mod]);
      mHist2D[kLGmeanM1 + mod]->Multiply(mHist2D[kLGoccupM1 + mod]);
      mHist2D[kLGrmsM1 + mod]->Multiply(mHist2D[kLGoccupM1 + mod]);
    }
    mFinalized = false;
  }

  for (const auto& tr : cellsTR) {
    int firstCellInEvent = tr.getFirstEntry();
    int lastCellInEvent = firstCellInEvent + tr.getNumberOfObjects();
    for (int i = firstCellInEvent; i < lastCellInEvent; i++) {
      const o2::phos::Cell c = cells[i];
      short address = c.getAbsId();
      char relid[3];
      o2::phos::Geometry::absToRelNumbering(address, relid);
      short mod = relid[0] - 1;
      if (c.getHighGain()) {
        mHist2D[kHGmeanM1 + mod]->Fill(relid[1] - 0.5, relid[2] - 0.5, c.getEnergy());
        mHist2D[kHGrmsM1 + mod]->Fill(relid[1] - 0.5, relid[2] - 0.5, 1.e+7 * c.getTime()); // to store in Cells format
        mHist2D[kHGoccupM1 + mod]->Fill(relid[1] - 0.5, relid[2] - 0.5);
      } else {
        mHist2D[kLGmeanM1 + mod]->Fill(relid[1] - 0.5, relid[2] - 0.5, c.getEnergy());
        mHist2D[kLGrmsM1 + mod]->Fill(relid[1] - 0.5, relid[2] - 0.5, 1.e+7 * c.getTime());
        mHist2D[kLGoccupM1 + mod]->Fill(relid[1] - 0.5, relid[2] - 0.5);
      }
    }
  }
}

void RawQcTask::CreatePedestalHistograms()
{
  // Prepare historams for pedestal run QA

  for (Int_t mod = 0; mod < 4; mod++) {
    if (!mHist2D[kHGmeanM1 + mod]) {
      mHist2D[kHGmeanM1 + mod] = new TH2F(Form("PedHGmean%d", mod + 1), Form("Pedestal mean High Gain, mod %d", mod + 1), 64, 0., 64., 56, 0., 56.);
      mHist2D[kHGmeanM1 + mod]->GetXaxis()->SetNdivisions(508, kFALSE);
      mHist2D[kHGmeanM1 + mod]->GetYaxis()->SetNdivisions(514, kFALSE);
      mHist2D[kHGmeanM1 + mod]->GetXaxis()->SetTitle("x, cells");
      mHist2D[kHGmeanM1 + mod]->GetYaxis()->SetTitle("z, cells");
      mHist2D[kHGmeanM1 + mod]->SetStats(0);
      mHist2D[kHGmeanM1 + mod]->SetMinimum(0);
      mHist2D[kHGmeanM1 + mod]->SetMaximum(100);
      getObjectsManager()->startPublishing(mHist2D[kHGmeanM1 + mod]);
    } else {
      mHist2D[kHGmeanM1 + mod]->Reset();
    }
    if (!mHist2D[kHGrmsM1 + mod]) {
      mHist2D[kHGrmsM1 + mod] = new TH2F(Form("PedHGrms%d", mod + 1), Form("Pedestal RMS High Gain, mod %d", mod + 1), 64, 0., 64., 56, 0., 56.);
      mHist2D[kHGrmsM1 + mod]->GetXaxis()->SetNdivisions(508, kFALSE);
      mHist2D[kHGrmsM1 + mod]->GetYaxis()->SetNdivisions(514, kFALSE);
      mHist2D[kHGrmsM1 + mod]->GetXaxis()->SetTitle("x, cells");
      mHist2D[kHGrmsM1 + mod]->GetYaxis()->SetTitle("z, cells");
      mHist2D[kHGrmsM1 + mod]->SetStats(0);
      mHist2D[kHGrmsM1 + mod]->SetMinimum(0);
      mHist2D[kHGrmsM1 + mod]->SetMaximum(2.);
      getObjectsManager()->startPublishing(mHist2D[kHGrmsM1 + mod]);
    } else {
      mHist2D[kHGrmsM1 + mod]->Reset();
    }
    if (!mHist2D[kHGoccupM1 + mod]) {
      mHist2D[kHGoccupM1 + mod] = new TH2F(Form("HGOccupancyM%d", mod + 1), Form("High Gain occupancy, mod %d", mod + 1), 64, 0., 64., 56, 0., 56.);
      mHist2D[kHGoccupM1 + mod]->GetXaxis()->SetNdivisions(508, kFALSE);
      mHist2D[kHGoccupM1 + mod]->GetYaxis()->SetNdivisions(514, kFALSE);
      mHist2D[kHGoccupM1 + mod]->GetXaxis()->SetTitle("x, cells");
      mHist2D[kHGoccupM1 + mod]->GetYaxis()->SetTitle("z, cells");
      mHist2D[kHGoccupM1 + mod]->SetStats(0);
      getObjectsManager()->startPublishing(mHist2D[kHGoccupM1 + mod]);
    } else {
      mHist2D[kHGoccupM1 + mod]->Reset();
    }
    if (!mHist2D[kLGmeanM1 + mod]) {
      mHist2D[kLGmeanM1 + mod] = new TH2F(Form("PedLGmean%d", mod + 1), Form("Pedestal mean Low Gain, mod %d", mod + 1), 64, 0., 64., 56, 0., 56.);
      mHist2D[kLGmeanM1 + mod]->GetXaxis()->SetNdivisions(508, kFALSE);
      mHist2D[kLGmeanM1 + mod]->GetYaxis()->SetNdivisions(514, kFALSE);
      mHist2D[kLGmeanM1 + mod]->GetXaxis()->SetTitle("x, cells");
      mHist2D[kLGmeanM1 + mod]->GetYaxis()->SetTitle("z, cells");
      mHist2D[kLGmeanM1 + mod]->SetStats(0);
      mHist2D[kLGmeanM1 + mod]->SetMinimum(0);
      mHist2D[kLGmeanM1 + mod]->SetMaximum(100);
      getObjectsManager()->startPublishing(mHist2D[kLGmeanM1 + mod]);
    } else {
      mHist2D[kLGmeanM1 + mod]->Reset();
    }
    if (!mHist2D[kLGrmsM1 + mod]) {
      mHist2D[kLGrmsM1 + mod] = new TH2F(Form("PedLGrms%d", mod + 1), Form("Pedestal RMS Low Gain, mod %d", mod + 1), 64, 0., 64., 56, 0., 56.);
      mHist2D[kLGrmsM1 + mod]->GetXaxis()->SetNdivisions(508, kFALSE);
      mHist2D[kLGrmsM1 + mod]->GetYaxis()->SetNdivisions(514, kFALSE);
      mHist2D[kLGrmsM1 + mod]->GetXaxis()->SetTitle("x, cells");
      mHist2D[kLGrmsM1 + mod]->GetYaxis()->SetTitle("z, cells");
      mHist2D[kLGrmsM1 + mod]->SetStats(0);
      mHist2D[kLGrmsM1 + mod]->SetMinimum(0);
      mHist2D[kLGrmsM1 + mod]->SetMaximum(2.);
      getObjectsManager()->startPublishing(mHist2D[kLGrmsM1 + mod]);
    } else {
      mHist2D[kLGrmsM1 + mod]->Reset();
    }
    if (!mHist2D[kLGoccupM1 + mod]) {
      mHist2D[kLGoccupM1 + mod] = new TH2F(Form("LGOccupancyM%d", mod + 1), Form("Low Gain occupancy, mod %d", mod + 1), 64, 0., 64., 56, 0., 56.);
      mHist2D[kLGoccupM1 + mod]->GetXaxis()->SetNdivisions(508, kFALSE);
      mHist2D[kLGoccupM1 + mod]->GetYaxis()->SetNdivisions(514, kFALSE);
      mHist2D[kLGoccupM1 + mod]->GetXaxis()->SetTitle("x, cells");
      mHist2D[kLGoccupM1 + mod]->GetYaxis()->SetTitle("z, cells");
      mHist2D[kLGoccupM1 + mod]->SetStats(0);
      getObjectsManager()->startPublishing(mHist2D[kLGoccupM1 + mod]);
    } else {
      mHist2D[kLGoccupM1 + mod]->Reset();
    }
    if (!mHist1D[kHGmeanSummaryM1 + mod]) {
      mHist1D[kHGmeanSummaryM1 + mod] = new TH1F(Form("PedHGMeanSum%d", mod + 1), Form("Pedestal HG mean summary, mod %d", mod + 1), 100, 0., 100.);
      mHist1D[kHGmeanSummaryM1 + mod]->GetXaxis()->SetTitle("ADC channels");
      // mHist1D[kHGmeanSummaryM1 + mod]->SetStats(0);
      mHist1D[kHGmeanSummaryM1 + mod]->SetMinimum(0);
      getObjectsManager()->startPublishing(mHist1D[kHGmeanSummaryM1 + mod]);
    } else {
      mHist1D[kHGmeanSummaryM1 + mod]->Reset();
    }
    if (!mHist1D[kHGrmsSummaryM1 + mod]) {
      mHist1D[kHGrmsSummaryM1 + mod] = new TH1F(Form("PedHGRMSSum%d", mod + 1), Form("Pedestal HG RMS summary, mod %d", mod + 1), 100, 0., 10.);
      mHist1D[kHGrmsSummaryM1 + mod]->GetXaxis()->SetTitle("ADC channels");
      // mHist1D[kHGrmsSummaryM1 + mod]->SetStats(0);
      mHist1D[kHGrmsSummaryM1 + mod]->SetMinimum(0);
      getObjectsManager()->startPublishing(mHist1D[kHGrmsSummaryM1 + mod]);
    } else {
      mHist1D[kHGrmsSummaryM1 + mod]->Reset();
    }
    if (!mHist1D[kLGmeanSummaryM1 + mod]) {
      mHist1D[kLGmeanSummaryM1 + mod] = new TH1F(Form("PedLGMeanSum%d", mod + 1), Form("Pedestal LG mean summary, mod %d", mod + 1), 100, 0., 100.);
      mHist1D[kLGmeanSummaryM1 + mod]->GetXaxis()->SetTitle("ADC channels");
      // mHist1D[kLGmeanSummaryM1 + mod]->SetStats(0);
      mHist1D[kLGmeanSummaryM1 + mod]->SetMinimum(0);
      getObjectsManager()->startPublishing(mHist1D[kLGmeanSummaryM1 + mod]);
    } else {
      mHist1D[kLGmeanSummaryM1 + mod]->Reset();
    }
    if (!mHist1D[kLGrmsSummaryM1 + mod]) {
      mHist1D[kLGrmsSummaryM1 + mod] = new TH1F(Form("PedLGRMSSum%d", mod + 1), Form("Pedestal LG RMS summary, mod %d", mod + 1), 100, 0., 10.);
      mHist1D[kLGrmsSummaryM1 + mod]->GetXaxis()->SetTitle("ADC channels");
      // mHist1D[kLGrmsSummaryM1 + mod]->SetStats(0);
      mHist1D[kLGrmsSummaryM1 + mod]->SetMinimum(0);
      getObjectsManager()->startPublishing(mHist1D[kLGrmsSummaryM1 + mod]);
    } else {
      mHist1D[kLGrmsSummaryM1 + mod]->Reset();
    }
  }
}
void RawQcTask::CreatePhysicsHistograms()
{
  // Prepare historams for pedestal run QA

  for (Int_t mod = 0; mod < 4; mod++) {
    if (!mHist2D[kCellOccupM1 + mod]) {
      mHist2D[kCellOccupM1 + mod] = new TH2F(Form("CellOccupancyM%d", mod + 1), Form("Cell occupancy, mod %d", mod + 1), 64, 0., 64., 56, 0., 56.);
      mHist2D[kCellOccupM1 + mod]->GetXaxis()->SetNdivisions(508, kFALSE);
      mHist2D[kCellOccupM1 + mod]->GetYaxis()->SetNdivisions(514, kFALSE);
      mHist2D[kCellOccupM1 + mod]->GetXaxis()->SetTitle("x, cells");
      mHist2D[kCellOccupM1 + mod]->GetYaxis()->SetTitle("z, cells");
      mHist2D[kCellOccupM1 + mod]->SetStats(0);
      mHist2D[kCellOccupM1 + mod]->SetMinimum(0);
      // mHist2D[kCellOccupM1+mod]->SetMaximum(100) ;
      getObjectsManager()->startPublishing(mHist2D[kCellOccupM1 + mod]);
    } else {
      mHist2D[kCellOccupM1 + mod]->Reset();
    }

    if (!mHist2D[kCellEM1 + mod]) {
      mHist2D[kCellEM1 + mod] = new TH2F(Form("CellEmean%d", mod + 1), Form("Cell mean energy, mod %d", mod + 1), 64, 0., 64., 56, 0., 56.);
      mHist2D[kCellEM1 + mod]->GetXaxis()->SetNdivisions(508, kFALSE);
      mHist2D[kCellEM1 + mod]->GetYaxis()->SetNdivisions(514, kFALSE);
      mHist2D[kCellEM1 + mod]->GetXaxis()->SetTitle("x, cells");
      mHist2D[kCellEM1 + mod]->GetYaxis()->SetTitle("z, cells");
      mHist2D[kCellEM1 + mod]->SetStats(0);
      mHist2D[kCellEM1 + mod]->SetMinimum(0);
      // mHist2D[kCellEM1+mod]->SetMaximum(1.) ;
      getObjectsManager()->startPublishing(mHist2D[kCellEM1 + mod]);
    } else {
      mHist2D[kCellEM1 + mod]->Reset();
    }

    if (!mHist2D[kTimeEM1 + mod]) {
      mHist2D[kTimeEM1 + mod] = new TH2F(Form("TimevsE%d", mod + 1), Form("Cell time vs energy, mod %d", mod + 1), 50, 0., 1000., 50, -5.e-7, 5.e-7);
      mHist2D[kTimeEM1 + mod]->GetXaxis()->SetNdivisions(508, kFALSE);
      mHist2D[kTimeEM1 + mod]->GetYaxis()->SetNdivisions(514, kFALSE);
      mHist2D[kTimeEM1 + mod]->GetXaxis()->SetTitle("Amp");
      mHist2D[kTimeEM1 + mod]->GetYaxis()->SetTitle("Time (ns)");
      mHist2D[kTimeEM1 + mod]->SetStats(0);
      mHist2D[kTimeEM1 + mod]->SetMinimum(0);
      getObjectsManager()->startPublishing(mHist2D[kTimeEM1 + mod]);
    } else {
      mHist2D[kTimeEM1 + mod]->Reset();
    }

    if (!mHist1D[kCellSpM1 + mod]) {
      mHist1D[kCellSpM1 + mod] = new TH1F(Form("CellSpectrumM%d", mod + 1), Form("Cell spectrum in mod %d", mod + 1), 100, 0., 1000.);
      mHist1D[kCellSpM1 + mod]->GetXaxis()->SetTitle("ADC channels");
      mHist1D[kCellSpM1 + mod]->SetStats(0);
      mHist1D[kCellSpM1 + mod]->SetMinimum(0);
      getObjectsManager()->startPublishing(mHist1D[kCellSpM1 + mod]);
    } else {
      mHist1D[kCellSpM1 + mod]->Reset();
    }
  }
}
void RawQcTask::CreateLEDHistograms()
{
  // Occupancy+mean+spectra
  CreatePhysicsHistograms();
  for (Int_t mod = 0; mod < 4; mod++) {
    if (!mHist2D[kLEDNpeaksM1 + mod]) {
      mHist2D[kLEDNpeaksM1 + mod] = new TH2F(Form("NLedPeaksM%d", mod + 1), Form("Number of LED peaks, mod %d", mod + 1), 64, 0., 64., 56, 0., 56.);
      mHist2D[kLEDNpeaksM1 + mod]->GetXaxis()->SetNdivisions(508, kFALSE);
      mHist2D[kLEDNpeaksM1 + mod]->GetYaxis()->SetNdivisions(514, kFALSE);
      mHist2D[kLEDNpeaksM1 + mod]->GetXaxis()->SetTitle("x, cells");
      mHist2D[kLEDNpeaksM1 + mod]->GetYaxis()->SetTitle("z, cells");
      mHist2D[kLEDNpeaksM1 + mod]->SetStats(0);
      mHist2D[kLEDNpeaksM1 + mod]->SetMinimum(0);
      getObjectsManager()->startPublishing(mHist2D[kLEDNpeaksM1 + mod]);
    } else {
      mHist2D[kLEDNpeaksM1 + mod]->Reset();
    }
  }
  // Prepare internal array of histos and final plot with number of peaks per channel
  mSpSearcher = std::make_unique<TSpectrum>(20);
  for (unsigned int absId = 1793; absId <= o2::phos::Mapping::NCHANNELS; absId++) {
    mSpectra.emplace_back(Form("SpChannel%d", absId), "", 487, 50., 1024.);
  }
}
} // namespace o2::quality_control_modules::phos
