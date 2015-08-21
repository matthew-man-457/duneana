// Module to calibrate EM shower energy

// Framework includes:
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Persistency/Common/Ptr.h"
#include "art/Persistency/Common/PtrVector.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "art/Framework/Services/Optional/TFileService.h"
#include "art/Framework/Services/Optional/TFileDirectory.h"
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "art/Framework/Core/EDAnalyzer.h"

// LArSoft includes
#include "Geometry/Geometry.h"
#include "Geometry/CryostatGeo.h"
#include "Geometry/TPCGeo.h"
#include "Geometry/PlaneGeo.h"
#include "RecoBase/Cluster.h"
#include "RecoBase/Hit.h"
#include "MCCheater/BackTracker.h"
#include "Utilities/AssociationUtil.h"
#include "Filters/ChannelFilter.h"
#include "SimulationBase/MCParticle.h"
#include "Simulation/ParticleList.h"
#include "Simulation/sim.h"

// ROOT & C++ includes
#include <string>
#include <vector>
#include <map>
#include "TTree.h"
#include "TBranch.h"
#include "TLeaf.h"

const int kMaxHits = 10000;

namespace emshower {
  class EMEnergyCalib;
}

class emshower::EMEnergyCalib : public art::EDAnalyzer {
public:

  EMEnergyCalib(fhicl::ParameterSet const& pset);
  void analyze(art::Event const& evt);
  void reconfigure(fhicl::ParameterSet const& p);
  void reset();

private:

  // Variables for the tree
  TTree* fTree;
  double trueEnergy;
  double depositU;
  double depositV;
  double depositZ;
  int    nhits;
  int    hit_tpc      [kMaxHits];
  int    hit_plane    [kMaxHits];
  int    hit_wire     [kMaxHits];
  int    hit_channel  [kMaxHits];
  double hit_peakT    [kMaxHits];
  double hit_charge   [kMaxHits];
  int    hit_clusterid[kMaxHits];

  std::string fHitsModuleLabel;
  std::string fClusterModuleLabel;
  art::ServiceHandle<art::TFileService> tfs;
  art::ServiceHandle<cheat::BackTracker> backtracker;
  art::ServiceHandle<geo::Geometry> geom;

};

emshower::EMEnergyCalib::EMEnergyCalib(fhicl::ParameterSet const& pset) : EDAnalyzer(pset) {
  this->reconfigure(pset);
  fTree = tfs->make<TTree>("EMEnergyCalib","EMEnergyCalib");
  fTree->Branch("TrueEnergy",   &trueEnergy);
  fTree->Branch("DepositU",     &depositU);
  fTree->Branch("DepositV",     &depositV);
  fTree->Branch("DepositZ",     &depositZ);
  fTree->Branch("NHits",        &nhits);
  fTree->Branch("Hit_TPC",      hit_tpc,      "hit_tpc[NHits]/I");
  fTree->Branch("Hit_Plane",    hit_plane,    "hit_plane[NHits]/I");
  fTree->Branch("Hit_Wire",     hit_wire,     "hit_wire[NHits]/I");
  fTree->Branch("Hit_Channel",  hit_channel,  "hit_channel[NHits]/I");
  fTree->Branch("Hit_PeakT",    hit_peakT,    "hit_peakT[NHits]/D");
  fTree->Branch("Hit_Charge",   hit_charge,   "hit_charge[NHits]/D");
  fTree->Branch("Hit_ClusterID",hit_clusterid,"hit_clusterid[NHits]/I");
}

void emshower::EMEnergyCalib::reconfigure(fhicl::ParameterSet const& pset) {
  fHitsModuleLabel = pset.get<std::string>("HitsModuleLabel");
  fClusterModuleLabel = pset.get<std::string>("ClusterModuleLabel");
}

void emshower::EMEnergyCalib::analyze(art::Event const& evt) {

  /// Analyse function to save information for calibrating shower energies
  /// This is written assuming single particle per event

  this->reset();

  // Get the hits out of the event record
  art::Handle<std::vector<recob::Hit> > hitHandle;
  std::vector<art::Ptr<recob::Hit> > hits;
  if (evt.getByLabel(fHitsModuleLabel,hitHandle))
    art::fill_ptr_vector(hits, hitHandle);

  // Get the clusters out of the event record
  art::Handle<std::vector<recob::Cluster> > clusterHandle;
  std::vector<art::Ptr<recob::Cluster> > clusters;
  if (evt.getByLabel(fClusterModuleLabel,clusterHandle))
    art::fill_ptr_vector(clusters, clusterHandle);

  art::FindManyP<recob::Cluster> fmc(hitHandle, evt, fClusterModuleLabel);

  // Look at the hits
  for (unsigned int hitIt = 0; hitIt < hits.size(); ++hitIt) {

    if (hitIt >= kMaxHits) continue;

    // Get the hit
    art::Ptr<recob::Hit> hit = hits.at(hitIt);

    // Fill hit level info
    hit_tpc    [hitIt] = hit->WireID().TPC;
    hit_plane  [hitIt] = hit->WireID().Plane;
    hit_wire   [hitIt] = hit->WireID().Wire;
    hit_peakT  [hitIt] = hit->PeakTime();
    hit_charge [hitIt] = hit->Integral();
    hit_channel[hitIt] = hit->Channel();

    // Find the cluster index this hit it associated with (-1 if unclustered)
    if (fmc.isValid()) {
      std::vector<art::Ptr<recob::Cluster> > clusters = fmc.at(hitIt);
      if (clusters.size() != 0) {
	hit_clusterid[hitIt] = clusters.at(0)->ID();
      }
      else hit_clusterid[hitIt] = -1;
    }

  }

  // Event level information

  nhits = hits.size();

  const sim::ParticleList& trueParticles = backtracker->ParticleList();
  const simb::MCParticle* trueParticle = trueParticles.Primary(0);

  trueEnergy = trueParticle->Momentum().E();

  // Find the energy deposited on each plane in the TPC
  const std::vector<const sim::SimChannel*>& simChannels = backtracker->SimChannels();
  for (std::vector<const sim::SimChannel*>::const_iterator channelIt = simChannels.begin(); channelIt != simChannels.end(); ++channelIt) {
    int plane = geom->View((*channelIt)->Channel());
    const std::map<unsigned short, std::vector<sim::IDE> >& tdcidemap = (*channelIt)->TDCIDEMap();
    for (std::map<unsigned short, std::vector<sim::IDE> >::const_iterator tdcIt = tdcidemap.begin(); tdcIt != tdcidemap.end(); ++tdcIt) {
      const std::vector<sim::IDE>& idevec = tdcIt->second;
      for (std::vector<sim::IDE>::const_iterator ideIt = idevec.begin(); ideIt != idevec.end(); ++ideIt) {
	switch (plane) {
	case 0:
	  depositU += ideIt->energy;
	  break;
	case 1:
	  depositV += ideIt->energy;
	  break;
	case 2:
	  depositZ += ideIt->energy;
	  break;
	}
      }
    }
  }

  fTree->Fill();

  return;

}

void emshower::EMEnergyCalib::reset() {
  trueEnergy = 0;
  depositU = 0;
  depositV = 0;
  depositZ = 0;
  nhits = 0;
  for (int hit = 0; hit < kMaxHits; ++hit) {
    hit_tpc[hit] = 0;
    hit_plane[hit] = 0;
    hit_wire[hit] = 0;
    hit_channel[hit] = 0;
    hit_peakT[hit] = 0;
    hit_charge[hit] = 0;
    hit_clusterid[hit] = 0;
  }
}

DEFINE_ART_MODULE(emshower::EMEnergyCalib)
