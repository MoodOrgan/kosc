#ifdef __INTELLISENSE__
#include "./stubs/Bela.h"
#include "./stubs/Gui.h"
#include "./stubs/GuiController.h"
#else
#include <Bela.h>
#include <libraries/Gui/Gui.h>
#include <libraries/GuiController/GuiController.h>
#endif
#include <stdlib.h>
#include <math.h>
#ifdef __INTELLISENSE__
#include "./stubs/math_neon.h"
#else
#include <libraries/math_neon/math_neon.h>
#endif

#define NUM_OSCS 15
#define NUM_CHANNELS 2
#define NUM_OUTPUT_NODES 15

Gui gui;
GuiController controller;
unsigned int gF0SliderIdx;
unsigned int gKSpreadSliderIdx;
unsigned int gInputRotateASliderIdx;
unsigned int gInputRotateBSliderIdx;
unsigned int gOutputRotateASliderIdx;
unsigned int gOutputRotateBSliderIdx;
unsigned int gNodeAttackSliderIdx;
unsigned int gNodeDecaySliderIdx;
// Dev-only macro control. Planned to map to CV (not an extra permanent UI control).
unsigned int gEnergyReserveSliderIdx;

float amplitude = 0.4f;

float omega[NUM_OSCS];
float theta[NUM_CHANNELS][NUM_OSCS];
float excitationScale[NUM_OSCS];
float injectionBoost[NUM_OSCS];

float gPrevF0 = 55.0f;

struct Biquad {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
    Biquad() : b0(0),b1(0),b2(0),a1(0),a2(0),x1(0),x2(0),y1(0),y2(0) {}
};

Biquad bpFilters[NUM_CHANNELS][NUM_OSCS];
float nodeEnvelope[NUM_CHANNELS][NUM_OSCS];
const float nodeBandwidth = 0.5f;

float couplingWeights[NUM_OSCS][NUM_OSCS];
float couplingWeightSum[NUM_OSCS];
float nodeBrightness[NUM_OSCS];
float gChannelEnergy[NUM_CHANNELS] = {0.0f, 0.0f};

// frequency-ordered: subharmonics ascending, f0, harmonics ascending
// node: 0      1      2      3      4      5      6      7    8    9    10   11   12   13   14
// mult: 1/8   1/7    1/6    1/5    1/4    1/3    1/2    1    2    3    4    5    6    7    8
const float mult[NUM_OSCS] = {
    1.0f/8.0f,
    1.0f/7.0f,
    1.0f/6.0f,
    1.0f/5.0f,
    1.0f/4.0f,
    1.0f/3.0f,
    1.0f/2.0f,
    1.0f,
    2.0f,
    3.0f,
    4.0f,
    5.0f,
    6.0f,
    7.0f,
    8.0f,
};

// output scanner: all 15 nodes
const int outputNodes[NUM_OUTPUT_NODES] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
};

float getFrequency(int i, float f0) {
    return f0 * mult[i];
}

int igcd(int a, int b) {
    while(b) { int t = b; b = a % b; a = t; }
    return a;
}

void buildCouplingWeights() {
    // asymmetric coupling: couplingWeights[i][j] = how strongly j drives i
    // ratio = mult[j]/mult[i] expressed as num_r/den_r in lowest terms
    // default law favors simple ratios with a soft cap to avoid 1.0 plateaus.
    // define KOSC_LEGACY_COUPLING to restore the original 1/num_r behavior.
    for(int i = 0; i < NUM_OSCS; i++) {
        couplingWeightSum[i] = 0.0f;
        for(int j = 0; j < NUM_OSCS; j++) {
            if(i == j) { couplingWeights[i][j] = 0.0f; continue; }

            // express mult[j]/mult[i] as integer fraction
            int pj = (mult[j] >= 1.0f) ? (int)roundf(mult[j])         : 1;
            int qj = (mult[j] >= 1.0f) ? 1                             : (int)roundf(1.0f/mult[j]);
            int pi = (mult[i] >= 1.0f) ? (int)roundf(mult[i])         : 1;
            int qi = (mult[i] >= 1.0f) ? 1                             : (int)roundf(1.0f/mult[i]);

            // ratio mult[j]/mult[i] = (pj/qj)/(pi/qi) = (pj*qi)/(qj*pi)
            int num = pj * qi;
            int den = qj * pi;
            int g   = igcd(num, den);
            int num_r = num / g;
            int den_r = den / g;

#ifdef KOSC_LEGACY_COUPLING
            float w = 1.0f / (float)num_r;
#else
            // Penalize both numerator and denominator complexity, then apply
            // directional bias: lower->higher tends to drive more than higher->lower.
            float complexity = (float)num_r + 0.35f * (float)(den_r - 1);
            float base = 1.0f / complexity;
            float asymmetry = (mult[j] < mult[i]) ? 1.15f : 0.8f;
            float w = fminf(base * asymmetry, 0.72f);
#endif
            couplingWeights[i][j] = (w > 0.035f) ? w : 0.0f;
            couplingWeightSum[i] += couplingWeights[i][j];
        }
    }
}

void buildExcitationScale() {
    for(int i = 0; i < NUM_OSCS; i++) {
        // excitationScale: reduces biquad input for subharmonics (harder to excite)
        excitationScale[i] = fminf(mult[i], 1.0f); // 0.125..1.0

        // injectionBoost: compensates by boosting injection at subharmonic nodes
        // sqrt compression keeps range reasonable: f0/8->2.83x, f0/2->1.41x
        // harmonics get 1.0 (no boost, no reduction)
        float boost = sqrtf(fmaxf(1.0f / mult[i], 1.0f));
        injectionBoost[i] = boost;

        // 0.0 at subharmonic floor (f0/8), 1.0 at harmonic ceiling (8*f0)
        nodeBrightness[i] = fminf(fmaxf(log2f(mult[i] * 8.0f) / 6.0f, 0.0f), 1.0f);
    }
}

void updateBiquadBP(Biquad &f, float freq, float Q, float sampleRate) {
    float w0    = 2.0f * M_PI * freq / sampleRate;
    float alpha = sinf_neon(w0) / (2.0f * Q);
    float cosw0 = cosf_neon(w0);
    float a0    = 1.0f + alpha;
    f.b0 =  alpha / a0;
    f.b1 =  0.0f;
    f.b2 = -alpha / a0;
    f.a1 = -2.0f * cosw0 / a0;
    f.a2 = (1.0f - alpha) / a0;
}

inline float processBiquad(Biquad &f, float in) {
    float out = f.b0*in + f.b1*f.x1 + f.b2*f.x2
              - f.a1*f.y1 - f.a2*f.y2;
    f.x2 = f.x1; f.x1 = in;
    f.y2 = f.y1; f.y1 = out;
    return out;
}

void updateFrequencies(BelaContext *context, float f0) {
    for(int i = 0; i < NUM_OSCS; i++) {
        float baseFreq     = getFrequency(i, f0);
        float neighborFreq = (i < NUM_OSCS - 1) ?
            getFrequency(i + 1, f0) : getFrequency(i - 1, f0);
        float spacing = fabsf(neighborFreq - baseFreq);
        float freq    = (baseFreq == 0.0f) ? 0.001f : baseFreq;
        float absFreq = fabsf(freq);
        omega[i] = 2.0f * M_PI * freq / context->audioSampleRate;
        float Q = absFreq / fmaxf(spacing, 1.0f) * nodeBandwidth;
        Q = fmaxf(0.5f, fminf(Q, 50.0f));
        for(int ch = 0; ch < NUM_CHANNELS; ch++)
            updateBiquadBP(bpFilters[ch][i], absFreq, Q, context->audioSampleRate);
    }
}

void initOscillators(BelaContext *context, float f0) {
    updateFrequencies(context, f0);
    for(int ch = 0; ch < NUM_CHANNELS; ch++)
        for(int i = 0; i < NUM_OSCS; i++) {
            theta[ch][i] = ((float)rand() / (float)RAND_MAX) * 2.0f * M_PI - M_PI;
            nodeEnvelope[ch][i] = 0.0f;
        }
}

bool setup(BelaContext *context, void *userData) {
    buildCouplingWeights();
    buildExcitationScale();

    rt_printf("Injection boosts:\n");
    for(int i = 0; i < NUM_OSCS; i++)
        rt_printf("  node %d (mult %.3f): excite=%.3f boost=%.3f net=%.3f\n",
            i, mult[i], excitationScale[i], injectionBoost[i],
            excitationScale[i] * injectionBoost[i]);

    rt_printf("couplingWeightSum[7] = %.3f\n", couplingWeightSum[7]);
    rt_printf("coupling weight f0/4->f0: %.3f\n", couplingWeights[7][4]);

    rt_printf("Coupling weight matrix:\n");
    for(int i = 0; i < NUM_OSCS; i++) {
        for(int j = 0; j < NUM_OSCS; j++)
            rt_printf("%.2f ", couplingWeights[i][j]);
        rt_printf("\n");
    }

    gui.setup(context->projectName);
    controller.setup(&gui, "Harmonic Resonator");

    gF0SliderIdx            = controller.addSlider("F0 (Hz)",           55.0,     0.0, 1024.0,  0.1);
    gKSpreadSliderIdx       = controller.addSlider("Spread",             0.5,     0.0,    2.0,  0.001);
    gInputRotateASliderIdx  = controller.addSlider("Input A Rotation",   0.5,     0.0,    1.0,  0.001);
    gInputRotateBSliderIdx  = controller.addSlider("Input B Rotation",   0.5,     0.0,    1.0,  0.001);
    gOutputRotateASliderIdx = controller.addSlider("Output A Rotation",  0.5,     0.0,    1.0,  0.001);
    gOutputRotateBSliderIdx = controller.addSlider("Output B Rotation",  0.5,     0.0,    1.0,  0.001);
    gNodeAttackSliderIdx    = controller.addSlider("Node Env Attack",    0.99,    0.9,  0.9999, 0.0001);
    gNodeDecaySliderIdx     = controller.addSlider("Node Env Decay",     0.9995,  0.9,  0.9999, 0.0001);
    gEnergyReserveSliderIdx = controller.addSlider("Energy Reserve (DEV)", 0.45,   0.0,    1.0,  0.001);

    srand(42);
    initOscillators(context, 55.0f);
    return true;
}

void render(BelaContext *context, void *userData) {
    float f0            = controller.getSliderValue(gF0SliderIdx);
    float KSpread       = controller.getSliderValue(gKSpreadSliderIdx);
    float inputRotateA  = controller.getSliderValue(gInputRotateASliderIdx);
    float inputRotateB  = controller.getSliderValue(gInputRotateBSliderIdx);
    float outputRotateA = controller.getSliderValue(gOutputRotateASliderIdx);
    float outputRotateB = controller.getSliderValue(gOutputRotateBSliderIdx);
    float nodeAttack    = controller.getSliderValue(gNodeAttackSliderIdx);
    float nodeDecay     = controller.getSliderValue(gNodeDecaySliderIdx);
    float energyReserve = controller.getSliderValue(gEnergyReserveSliderIdx);

    // Avoid filter coefficient churn from tiny slider jitter.
    if(fabsf(f0 - gPrevF0) > 1e-6f) {
        updateFrequencies(context, f0);
        gPrevF0 = f0;
    }

    // input scanner A — ring 0, all 15 nodes, frequency ordered
    float inPosA       = inputRotateA * NUM_OSCS;
    int   inNodeA      = (int)inPosA % NUM_OSCS;
    float inFracA      = inPosA - (int)inPosA;
    float inGainA      = cosf_neon(inFracA * M_PI / 2.0f);
    float inGainA_next = sinf_neon(inFracA * M_PI / 2.0f);
    int   inNodeA_next = (inNodeA + 1) % NUM_OSCS;

    // input scanner B — ring 1, all 15 nodes, frequency ordered
    float inPosB       = inputRotateB * NUM_OSCS;
    int   inNodeB      = (int)inPosB % NUM_OSCS;
    float inFracB      = inPosB - (int)inPosB;
    float inGainB      = cosf_neon(inFracB * M_PI / 2.0f);
    float inGainB_next = sinf_neon(inFracB * M_PI / 2.0f);
    int   inNodeB_next = (inNodeB + 1) % NUM_OSCS;

    // output scanner A — ring 0, all 15 nodes
    float outPosA       = outputRotateA * NUM_OUTPUT_NODES;
    int   outIdxA       = (int)outPosA % NUM_OUTPUT_NODES;
    float outFracA      = outPosA - (int)outPosA;
    int   outNodeA      = outputNodes[outIdxA];
    int   outNodeA_next = outputNodes[(outIdxA + 1) % NUM_OUTPUT_NODES];
    float gainNodeA     = cosf_neon(outFracA * M_PI / 2.0f);
    float gainNextA     = sinf_neon(outFracA * M_PI / 2.0f);

    // output scanner B — ring 1, all 15 nodes
    float outPosB       = outputRotateB * NUM_OUTPUT_NODES;
    int   outIdxB       = (int)outPosB % NUM_OUTPUT_NODES;
    float outFracB      = outPosB - (int)outPosB;
    int   outNodeB      = outputNodes[outIdxB];
    int   outNodeB_next = outputNodes[(outIdxB + 1) % NUM_OUTPUT_NODES];
    float gainNodeB     = cosf_neon(outFracB * M_PI / 2.0f);
    float gainNextB     = sinf_neon(outFracB * M_PI / 2.0f);

    const int   inNode[NUM_CHANNELS]      = { inNodeA,      inNodeB      };
    const int   inNodeNext[NUM_CHANNELS]  = { inNodeA_next, inNodeB_next };
    const float inGain[NUM_CHANNELS]      = { inGainA,      inGainB      };
    const float inGainNext[NUM_CHANNELS]  = { inGainA_next, inGainB_next };

    for(int frame = 0; frame < context->audioFrames; frame++) {

        float input[NUM_CHANNELS];
        for(int ch = 0; ch < NUM_CHANNELS; ch++) {
            input[ch] = audioRead(context, frame, ch);
        }

        float oscOut[NUM_CHANNELS][NUM_OSCS];

        for(int ch = 0; ch < NUM_CHANNELS; ch++) {
            // per-node: bandpass, envelope, phase accumulation
            for(int i = 0; i < NUM_OSCS; i++) {
                // Injection scanner only addresses two adjacent nodes.
                float inj = 0.0f;
                if(i == inNode[ch])     inj += inGain[ch];
                if(i == inNodeNext[ch]) inj += inGainNext[ch];
                inj = fminf(inj, 1.0f);

                // excitationScale reduces subharmonic injection (harder to excite)
                // injectionBoost compensates so subharmonics still respond
                // net effect: sqrt(mult[i]) — compressed asymmetry
                float bandSig = processBiquad(bpFilters[ch][i],
                    input[ch] * inj
                    * excitationScale[i] * injectionBoost[i]);
                float target  = fabsf(bandSig);

                // Frequency-dependent damping:
                // - darker/lower nodes ring longer
                // - brighter/higher nodes lose energy faster
                // The global slider still defines the base decay feel.
                const float maxDecayCeil = 0.9999f;
                const float minDecayFloor = 0.97f;
                const float lowRingBias = 0.25f;
                const float highDampBias = 0.35f;
                float b = nodeBrightness[i];
                float slowerDecay = nodeDecay + (maxDecayCeil - nodeDecay) * lowRingBias * (1.0f - b);
                float effectiveDecay = slowerDecay - (slowerDecay - minDecayFloor) * highDampBias * b;

                if(target > nodeEnvelope[ch][i])
                    nodeEnvelope[ch][i] = nodeAttack * nodeEnvelope[ch][i]
                                        + (1.0f - nodeAttack) * target;
                else
                    nodeEnvelope[ch][i] = effectiveDecay  * nodeEnvelope[ch][i]
                                        + (1.0f - effectiveDecay)  * target;

                theta[ch][i] += omega[i];
                if(theta[ch][i] >  M_PI) theta[ch][i] -= 2.0f * M_PI;
                if(theta[ch][i] < -M_PI) theta[ch][i] += 2.0f * M_PI;

                oscOut[ch][i] = sinf_neon(theta[ch][i]) * nodeEnvelope[ch][i];
            }

            // asymmetric harmonic diffusion — max-based, no normalization dilution
            float prevEnv[NUM_OSCS];
            for(int i = 0; i < NUM_OSCS; i++) prevEnv[i] = nodeEnvelope[ch][i];

            for(int i = 0; i < NUM_OSCS; i++) {
                float maxContrib = 0.0f;
                for(int j = 0; j < NUM_OSCS; j++) {
                    float contrib = prevEnv[j] * couplingWeights[i][j];
                    if(contrib > maxContrib) maxContrib = contrib;
                }
                nodeEnvelope[ch][i] = fminf(
                    fmaxf(nodeEnvelope[ch][i], maxContrib * KSpread),
                    1.05f
                );
            }

            // Limited energy store per channel:
            // external input replenishes energy, while active resonance spends it.
            float inputAbs = fabsf(input[ch]);
            // Nonlinear mapping for stronger audible range from the same 0..1 control.
            float reserveCurve = energyReserve * energyReserve;
            float activeBudgetScale = 0.2f + 1.8f * reserveCurve;
            float replenishScale = 0.0003f + 0.02f * reserveCurve;
            float leakScale = 0.004f * (1.0f - reserveCurve) + 0.00008f;

            float replenish = replenishScale * inputAbs;
            gChannelEnergy[ch] = fminf(gChannelEnergy[ch] + replenish, 2.2f);

            float sumSq = 0.0f;
            for(int i = 0; i < NUM_OSCS; i++)
                sumSq += nodeEnvelope[ch][i] * nodeEnvelope[ch][i];
            float meanSq = sumSq / (float)NUM_OSCS;

            float budgetMeanSq = (0.002f + 0.01f * reserveCurve)
                               + activeBudgetScale * 0.22f * gChannelEnergy[ch];
            if(meanSq > budgetMeanSq && meanSq > 1e-9f) {
                float scale = sqrtf(budgetMeanSq / meanSq);
                for(int i = 0; i < NUM_OSCS; i++)
                    nodeEnvelope[ch][i] *= scale;
            }

            float spreadSpend = 0.0012f + 0.0055f * KSpread;
            float spend = meanSq * spreadSpend;
            float leak  = leakScale;
            gChannelEnergy[ch] = fmaxf(gChannelEnergy[ch] - spend - leak, 0.0f);
        }

        float outA = oscOut[0][outNodeA] * gainNodeA
                   + oscOut[0][outNodeA_next] * gainNextA;
        float outB = oscOut[1][outNodeB] * gainNodeB
                   + oscOut[1][outNodeB_next] * gainNextB;

        audioWrite(context, frame, 0, outA * amplitude);
        audioWrite(context, frame, 1, outB * amplitude);
    }
}

void cleanup(BelaContext *context, void *userData) {}