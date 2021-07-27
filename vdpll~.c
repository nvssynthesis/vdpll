#include "../m_pd.h"
#include <math.h>
#include <string.h>
#include "ringmods.h"
#ifdef NT
#pragma warning( disable : 4244 )
#pragma warning( disable : 4305 )
#endif
#define PI 3.141592653589793
#define TWOPI 6.283185307179586


/* ------------------------ vdpll~ ----------------------------- */

/*
 phase locked loop object.
 several options for phase detection circuit:
    -ideal ring modulator (four-quadrant multiplier)
    -analog-style ringmod,  taken from mutable instruments' Warps
    -digital-style ringmod, taken from mutable instruments' Warps
    -XOR (1-bit ring modulator)

 ARGUMENTS:
    -first argument sets internal frequency. Defaults to 1000 Hz.
    -second argument sets k. Defaults to 0.
    -third agument sets cutoff frequency. Defaults to 1 Hz.
    -fourth argument sets phase detector type. Defaults to ideal.
 INLET:
    -signal in is the control signal to lock phase to
    -control-rate data determines internal frequency
 OUTLETS:
 	-the internal oscillator
 	-the internal phase (useful for controlling arbitrary wavetable)
 TODO:
    -try other filter implementations
 */

static t_class *vdpll_tilde_class;

typedef struct _vdpll_tilde
{
    t_object x_obj; 	/* obligatory header */
    
    t_float intern_freq, intern_freq_target, cutoff, cutOverFs, cutOverFs_target, k, k_target;
    
    float fs_delta;
    float last_lop_out, last_out;
    double phase;
    enum phase_detector_type {
        ideal,   //0
        analog,  //1
        digital, //2
        xor      //3
    } pdType;
    
    t_outlet*phasor_out;
    
} t_vdpll_tilde;

// onepole lowpass
// REQUIRES MANUAL INPUT OF LAST OUTPUT.
// PRE-COMPUTE 1/FS * CUTOFF
float lop(float in, float out_z1, float cutOverFs)
{
    float x = exp(-TWOPI * cutOverFs);
    float a0 = 1.f - x;
    float b1 = -x;
    float out = a0 * in - b1 * out_z1;
    return out;
}

static void vdpll_tilde_post(t_vdpll_tilde *x)
{
    post("cutOverFs is %f", x->cutOverFs);
    post("k is %f", x->k);
    post("intern_freq is %f", x->intern_freq);

    switch (x->pdType)
        {
            case 0: post("phase detection method is ideal ring modulator (multiplier)");
                break;
            case 1: post("phase detection method is analog ring modulator");
                break;
            case 2: post("phase detection method is digital ring modulator");
                break;
            case 3: post("phase detection method is xor ring modulator");
                break;
        }
}

static void vdpll_tilde_set_freq(t_vdpll_tilde *x, t_floatarg freq)
{
    x->intern_freq_target = freq;
    //post("intern_freq_target is %f", x->intern_freq_target);
}
static void vdpll_tilde_set_k(t_vdpll_tilde *x, t_floatarg k)
{
    if (k > 10000.f)
    {
        k = 10000.f;
        post("k maxed out");
    }

    x->k_target = (float)k;
    //post("k_target is %f", x->k_target);
}
static void vdpll_tilde_set_cutoff(t_vdpll_tilde *x, t_floatarg cutoff)
{
    cutoff = cutoff * (cutoff > 0.f);
    x->cutoff = cutoff;
    x->cutOverFs_target = (float)cutoff * (float)x->fs_delta;
}

static void vdpll_tilde_set_phase_detector(t_vdpll_tilde *x, t_float type)
{
    if ((type >= 0) && (type < 4))
        x->pdType = (int)(floor(type));
    else
        post("enter 0 for 'ideal', 1 for 'analog', 2 for 'digital', or 3 for 'xor'");
}

static t_int *vdpll_tilde_perform(t_int *w)
{	
// x,
// input vector
// main output vector
// phase output vector
// blocksize

    t_vdpll_tilde *x = (t_vdpll_tilde *)(w[1]);
    
    t_float *master  = (t_float *)(w[2]);
    t_float *vco_out = (t_float *)(w[3]);
    t_float *phase_out = (t_float *)(w[4]);
    int n = (int)(w[5]);
    
    float modulatorOut;
    float phase = x->phase;
    // copy from struct only once per block
    float last_lop_out = x->last_lop_out;
    float fs_delta = x->fs_delta;
    
    while (n--)
    {
        if (x->intern_freq != x->intern_freq_target)
            x->intern_freq += (x->intern_freq_target - x->intern_freq);
        if (x->k != x->k_target)
            x->k += (x->k_target - x->k);
        if (x->cutOverFs != x->cutOverFs_target)
            x->cutOverFs += (x->cutOverFs_target - x->cutOverFs);

        switch (x->pdType)
        {
            case 0:     // ideal ringmod
            {
                modulatorOut = x->last_out * *master;
                break;
            }
            case 1:
            {
                modulatorOut = analog_ringmod(x->last_out, *master, 0.f);
                break;
            }
            case 2:
            {
                modulatorOut = digital_ringmod(x->last_out, *master, 0.f);
                break;
            }
            case 3:
            {
                float onebit_master = (*master > 0.f) ? 1.f : -1.f;
                float onebit_intern = (x->last_out > 0.f) ? 1.f : -1.f;
                // XOR
                modulatorOut = (onebit_intern != onebit_master) * 2.f - 1.f;
                break;
            }
        }
        
        // filter the modulator output to isolate DC component.
        last_lop_out = lop(modulatorOut, last_lop_out, x->cutOverFs);
        
        float phaseinc = (x->intern_freq * x->fs_delta) + (last_lop_out * x->k * x->intern_freq * x->fs_delta);
		phaseinc = phaseinc <  0.5f ? phaseinc :  0.5f;
		phaseinc = phaseinc > -0.5f ? phaseinc : -0.5f;
        phase += phaseinc;

        // wrap between 0 and 1
        while (phase >= 1.f)
            phase -= 1.f;
        while (phase < 0)
            phase += 1.f;
        
    	float f = *(master++);        
        x->last_out = cos(phase * TWOPI);
        *vco_out++ = x->last_out;
        *phase_out++ = phase;
        }
        
    x->last_lop_out = last_lop_out;
    x->phase = phase;
    
    return (w+6);
    
}

    /* called to start DSP.  Here we call Pd back to add our perform
    routine to a linear callback list which Pd in turn calls to grind
    out the samples. */
static void vdpll_tilde_dsp(t_vdpll_tilde *x, t_signal **sp)
{	 
	
    x->fs_delta = 1.0f / sp[0]->s_sr;
    x->cutOverFs = x->cutoff * x->fs_delta;
    
	// RESET FILTER STATES TOO
	x->last_lop_out = 0.f;
    x->last_out = 0.f;

    dsp_add(vdpll_tilde_perform,
            5,              // number of items
            x,
            sp[0]->s_vec,   // input vector
            sp[1]->s_vec,   // output vector
            sp[2]->s_vec,   // output vector
            sp[0]->s_n);    // blocksize
            
}

static void *vdpll_tilde_new(t_float freq, t_float k, t_float cut, t_float type)
{
    t_vdpll_tilde *x = (t_vdpll_tilde *)pd_new(vdpll_tilde_class);
    outlet_new(&x->x_obj, gensym("signal"));
    
    x->phasor_out = outlet_new(&x->x_obj, &s_signal);

    x->intern_freq = 1000.f;
    x->k = 1000.f;
    x->cutoff = 1;
    x->pdType = ideal;
    //============================================================
    x->intern_freq= freq;
    x->intern_freq_target = freq;
    x->k =          k;
    x->k_target =   k;
    x->cutoff =     cut;
    x->cutOverFs =  cut / 44100.f;
    x->cutOverFs_target = cut / 44100.f;
    
    if ((type >= 0) && (type < 4))
        x->pdType = (int)(floor(type));
    else
        post("enter 0 for 'ideal', 1 for 'analog', 2 for 'digital', or 3 for 'xor'");
    //============================================================
    return (x);
}

    /* this routine, which must have exactly this name (with the "~" replaced
    by "_tilde) is called when the code is first loaded, and tells Pd how
    to build the "class". */
void vdpll_tilde_setup(void)
{
    vdpll_tilde_class = class_new(gensym("vdpll~"), (t_newmethod)vdpll_tilde_new, 0,
    	sizeof(t_vdpll_tilde), 0, A_DEFFLOAT, A_DEFFLOAT, A_DEFFLOAT, A_DEFFLOAT, 0);
    class_addbang(vdpll_tilde_class, (t_method)vdpll_tilde_post);
    class_addmethod(vdpll_tilde_class, (t_method)vdpll_tilde_set_freq, gensym("frequency"), A_FLOAT, 0);
    class_addmethod(vdpll_tilde_class, (t_method)vdpll_tilde_set_k, gensym("k"), A_FLOAT, 0);
    class_addmethod(vdpll_tilde_class, (t_method)vdpll_tilde_set_cutoff, gensym("cutoff"), A_FLOAT, 0);
    class_addmethod(vdpll_tilde_class, (t_method)vdpll_tilde_set_phase_detector, gensym("detector"), A_FLOAT, 0);
	    /* this is magic to declare that the leftmost, "main" inlet
	    takes signals; other signal inlets are done differently... */
    CLASS_MAINSIGNALIN(vdpll_tilde_class, t_vdpll_tilde, intern_freq);
    	/* here we tell Pd about the "dsp" method, which is called back
	when DSP is turned on. */
    class_addmethod(vdpll_tilde_class, (t_method)vdpll_tilde_dsp, gensym("dsp"), 0);
    class_sethelpsymbol(vdpll_tilde_class, gensym("vdpll~"));
}
