/* 由 export_recipes.py 自动生成，勿手改 */
#ifndef RECIPES_GEN_H_
#define RECIPES_GEN_H_

typedef struct {
    const char *id;
    const char *name;
    double matrix[3][3];
    double saturation;
    double contrast;
    double shadow_lift;
    double highlight_rolloff;
    double grain_intensity;
    double grain_size;
    int grain_mono;
    double strength;
} FilmRecipe;

static const FilmRecipe RECIPES[] = {
  {"ektachrome", "Kodak Ektachrome E100", {{0.97,0.03,0.05},{0,1.06,0.02},{0.02,-0.03,1.07}}, 1.2, 1.1, -0.02, 0.03, 0.12, 1, 0, 1},
  {"hp5", "Ilford HP5 Plus", {{0.34,0.56,0.1},{0.34,0.56,0.1},{0.34,0.56,0.1}}, 0, 1, 0.01, 0.05, 0.42, 1.8, 1, 1},
  {"portra400", "Kodak Portra 400", {{1.06,0.04,-0.02},{0.01,1,0.02},{-0.02,0.05,0.92}}, 0.95, 0.85, 0.03, 0.07, 0.3, 1.2, 0, 1},
  {"superia400", "Fuji Superia 400", {{1.02,0.05,0.02},{0,0.99,0.04},{-0.01,0.02,1.04}}, 1.05, 0.9, 0.02, 0.06, 0.35, 1.5, 0, 1},
  {"trix400", "Ilford Tri-X 400", {{0.3,0.58,0.12},{0.3,0.58,0.12},{0.3,0.58,0.12}}, 0, 1.15, 0.01, 0.04, 0.55, 2.5, 1, 1},
  {"velvia50", "Fuji Velvia 50", {{1.02,-0.04,0.02},{-0.03,1.1,-0.02},{0,-0.06,1.08}}, 1.3, 1.25, -0.02, 0.03, 0.12, 1, 0, 1},
};

#define NUM_RECIPES (int)(sizeof(RECIPES)/sizeof(RECIPES[0]))

#endif /* RECIPES_GEN_H_ */
