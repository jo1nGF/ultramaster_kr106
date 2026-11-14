#pragma once
#include "DSP/KR106Varigate.h"
#include "DSP/KR106Metropolix.h"
class KR106AudioProcessor { public: kr106::VarigateSeq seq; kr106::MetropolixSeq metro; int mode = 0;
  kr106::VarigateSeq& getSeq() { return seq; } kr106::MetropolixSeq& getMetro() { return metro; }
  int getSeqMode() const { return mode; } void setSeqMode(int m) { mode = m ? 1 : 0; } };
