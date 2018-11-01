
#define STAGES    7
#define OUT_IDX   (3 * STAGES)

static float32_t a[3 * STAGES + 3];     // Filter a variables
static float32_t b[3 * STAGES + 3];     // Filter b variables
static float32_t c[3 * STAGES + 3];     // Filter c variables
static float32_t d[3 * STAGES + 3];     // Filter d variables
const float32_t c0[STAGES];          // Filter coefficients - path 0
const float32_t c1[STAGES];          // Filter coefficients - path 1
static float32_t dsI;             // delayed sample, I path
static float32_t dsQ;             // delayed sample, Q path
uint8_t sbmode;             // sideband mode
uint8_t levelfade;            // Fade Leveler switch

    //sideband separation
  c0[0] = -0.328201924180698;
  c0[1] = -0.744171491539427;
  c0[2] = -0.923022915444215;
  c0[3] = -0.978490468768238;
  c0[4] = -0.994128272402075;
  c0[5] = -0.998458978159551;
  c0[6] = -0.999790306259206;
     
  c1[0] = -0.0991227952747244;
  c1[1] = -0.565619728761389;
  c1[2] = -0.857467122550052;
  c1[3] = -0.959123933111275;
  c1[4] = -0.988739372718090;
  c1[5] = -0.996959189310611;
  c1[6] = -0.999282492800792;


void flush_amd (AMD a)
{
  a->dc = 0.0;
  a->dc_insert = 0.0;
}

void xamd (AMD a)
{
  int i;
  double audio;
  double vco[2];
  double corr[2];
  double det;
  double del_out;
  double ai, bi, aq, bq;
  double ai_ps, bi_ps, aq_ps, bq_ps;
  int j, k;
  if (a->run)
  {
    switch (a->mode)
    {

      case 0:   //AM Demodulator
        {
          for (i = 0; i < a->buff_size; i++)
          {
            audio = sqrt(a->in_buff[2 * i + 0] * a->in_buff[2 * i + 0] + a->in_buff[2 * i + 1] * a->in_buff[2 * i + 1]);
            if (a->levelfade)
            {
              a->dc = a->mtauR * a->dc + a->onem_mtauR * audio;
              a->dc_insert = a->mtauI * a->dc_insert + a->onem_mtauI * audio;
              audio += a->dc_insert - a->dc;
            }
            a->out_buff[2 * i + 0] = audio;
            a->out_buff[2 * i + 1] = audio;
          }
          break;
        }

      case 1:   //Synchronous AM Demodulator with Sideband Separation
        {
          for (i = 0; i < a->buff_size; i++)
          {
            vco[0] = cos(a->phs);
            vco[1] = sin(a->phs);

            ai = a->in_buff[2 * i + 0] * vco[0];
            bi = a->in_buff[2 * i + 0] * vco[1];
            aq = a->in_buff[2 * i + 1] * vco[0];
            bq = a->in_buff[2 * i + 1] * vco[1];

            if (a->sbmode != 0)
            {
              a[0] = dsI;
              b[0] = bi;
              c[0] = dsQ;
              d[0] = aq;
              dsI = ai;
              dsQ = bq;

              for (j = 0; j < STAGES; j++)
              {
                k = 3 * j;
                a[k + 3] = c0[j] * (a[k] - a[k + 5]) + a[k + 2];
                b[k + 3] = c1[j] * (b[k] - b[k + 5]) + b[k + 2];
                c[k + 3] = c0[j] * (c[k] - c[k + 5]) + c[k + 2];
                d[k + 3] = c1[j] * (d[k] - d[k + 5]) + d[k + 2];
              }
              ai_ps = a[OUT_IDX];
              bi_ps = b[OUT_IDX];
              bq_ps = c[OUT_IDX];
              aq_ps = d[OUT_IDX];

              for (j = OUT_IDX + 2; j > 0; j--)
              {
                a[j] = a[j - 1];
                b[j] = b[j - 1];
                c[j] = c[j - 1];
                d[j] = d[j - 1];
              }
            }

            corr[0] = +ai + bq;
            corr[1] = -bi + aq;

            switch(a->sbmode)
            {
            case 0: //both sidebands
              {
                audio = corr[0];
                break;
              }
            case 1: //LSB
              {
                audio = (ai_ps - bi_ps) + (aq_ps + bq_ps);
                break;
              }
            case 2: //USB
              {
                audio = (ai_ps + bi_ps) - (aq_ps - bq_ps);
                break;
              }
            }

            if (a->levelfade)
            {
              a->dc = a->mtauR * a->dc + a->onem_mtauR * audio;
              a->dc_insert = a->mtauI * a->dc_insert + a->onem_mtauI * corr[0];
              audio += a->dc_insert - a->dc;
            }
            a->out_buff[2 * i + 0] = audio;
            a->out_buff[2 * i + 1] = audio;

            if ((corr[0] == 0.0) && (corr[1] == 0.0)) corr[0] = 1.0;
            det = atan2(corr[1], corr[0]);
            del_out = a->fil_out;
            a->omega += a->g2 * det;
            if (a->omega < a->omega_min) a->omega = a->omega_min;
            if (a->omega > a->omega_max) a->omega = a->omega_max;
            a->fil_out = a->g1 * det + a->omega;
            a->phs += del_out;
            while (a->phs >= TWOPI) a->phs -= TWOPI;
            while (a->phs < 0.0) a->phs += TWOPI;
          }
          break;
        }
    }
  }
  else if (a->in_buff != a->out_buff)
    memcpy (a->out_buff, a->in_buff, a->buff_size * sizeof(complex));
}

void setBuffers_amd (AMD a, double* in, double* out)
{
  a->in_buff = in;
  a->out_buff = out;
}

void setSamplerate_amd (AMD a, int rate)
{
  a->sample_rate = rate;
  init_amd(a);
}

void setSize_amd (AMD a, int size)
{
  a->buff_size = size;
}

