// Original tessellated ramp-and-crosswalk experiment; no archive geometry.
#include "recipe_support.hpp"
using namespace qmg;

int main(int argc, char **argv) try {
    if (argc != 2) throw std::runtime_error("usage: qmg_ravel_recipe <output directory>");
    recipes::Recipe r("qmg_ravel", "Ravel",
        "Compact asymmetric Quake II textile works, tessellated unequal rooms, long ramp approach, "
        "crossing above a second route, local third floor and ordinary return", 20260919, 6);
    r.floor_levels={0,128,256};
    r.require_ordinary_routes=r.require_no_drop_routes=true;
    r.room("Spindle",{{-848,-720,0},{-288,272,448}});
    r.room("Crossing",{{-240,-720,0},{320,80,448}});
    r.room("Crown",{{-240,128,128},{320,720,416}});
    r.room("Return",{{368,-224,0},{848,720,384}});
    r.room("Dock",{{368,-720,0},{848,-272,320}});
    r.room("Header",{{-848,320,128},{-288,720,384}});
    r.connection(1,2,160,{{{-288,-640,0},{-240,-480,192}}});
    r.connection(1,2,160,{{{-288,-112,128},{-240,48,320}}});
    r.connection(1,6,160,{{{-784,272,128},{-624,320,320}}});
    r.connection(2,3,160,{{{-224,80,128},{-64,128,320}}});
    r.connection(6,3,160,{{{-288,464,128},{-240,624,320}}});
    r.connection(3,4,160,{{{320,288,128},{368,448,320}}});
    r.connection(4,5,160,{{{592,-272,0},{752,-224,192}}});
    r.connection(2,5,160,{{{320,-592,0},{368,-432,192}}});
    r.connection(2,4,160,{{{320,-144,0},{368,16,192}}});

    // The long ramp offers an exposed ascent; its high end turns into two exits.
    r.ramp({-816,-592,0},HorizontalDirection::positive_y,160);
    r.deck(-848,-112,-288,48,128);
    r.deck(-848,48,-624,272,128);
    r.solid({{-512,-336,0},{-400,-208,144}});
    r.prism({{-384,-720},{-288,-720},{-288,-624}},0,448);
    r.solid({{-528,112,0},{-432,208,112}});
    r.solid({{-784,80,128},{-720,144,240}});

    // A clean middle crosswalk has clear floor-zero space on both sides.
    // The third floor occupies only the northeast edge, reached by its own stair.
    r.deck(-240,-400,320,-240,128);
    r.deck(-240,-240,-80,80,128);
    r.deck(-80,-80,320,80,128);
    r.stairs(-192,-688,0,1,8);
    r.stairs(192,-336,128,1,8);
    r.deck(160,-48,320,80,256);
    r.solid({{160,-48,256},{224,-16,352}});
    r.drop_specs.push_back({"Crown ledge to crosswalk return",{176,16,256},{112,16,256},{112,16,128}});
    r.solid({{96,-656,0},{176,-560,128}});
    r.solid({{-224,-64,128},{-160,0,224}});

    // An inset loom makes the upper Crown an actual bent encounter.
    r.solid({{-240,352,128},{-64,496,416}});
    r.solid({{160,544,128},{224,640,240}});
    r.prism({{224,720},{320,624},{320,720}},128,416);

    // The eastern gallery bends around its floor-zero lane rather than copying
    // the western ramp; its stair faces the compact southern dock.
    r.deck(368,-224,528,720,128);
    r.deck(528,560,848,720,128);
    r.deck(688,160,848,560,128);
    r.stairs(688,-128,0,1,8);
    r.solid({{560,352,0},{624,448,128}});
    r.solid({{400,528,128},{464,592,240}});

    r.solid({{544,-592,0},{608,-528,112}});
    r.prism({{752,-720},{848,-720},{848,-624}},0,320);
    r.solid({{-608,560,128},{-512,656,272}});
    r.prism({{-848,624},{-752,720},{-848,720}},128,384);

    r.spawn({-352,-576,24},90);
    r.spawn({-672,208,152},0);
    r.spawn({-16,-640,24},90);
    r.spawn({256,32,280},180);
    r.spawn({16,640,152},270);
    r.spawn({768,640,152},180);
    r.spawn({448,-640,24},90);
    r.spawn({-352,656,152},180);
    r.point(PlacementRole::weapon,{-576,-512,24},4);
    r.point(PlacementRole::weapon,{-16,-144,24},0,"weapon_supershotgun");
    r.point(PlacementRole::weapon,{256,-16,280},3);
    r.point(PlacementRole::weapon,{48,256,152},6);
    r.point(PlacementRole::weapon,{768,320,152},5);
    r.point(PlacementRole::weapon,{752,-368,24},0);
    r.point(PlacementRole::weapon,{-752,400,152},2);
    r.point(PlacementRole::armor,{-352,176,24});
    r.point(PlacementRole::armor,{448,224,152});
    r.point(PlacementRole::health,{112,-320,24},0,"item_health_mega");
    r.point(PlacementRole::health,{-432,400,152},0,"item_quad");
    for(auto p:{GridVec3{-464,-32,152},{-720,208,152},{-16,-512,24},{112,16,152},
                {240,480,152},{592,640,24},{464,-368,24},{-720,608,152}})
        r.point(PlacementRole::health,p);
    r.point(PlacementRole::health,{-512,-512,24},0,"ammo_rockets");
    r.point(PlacementRole::health,{48,-144,24},0,"ammo_shells");
    r.point(PlacementRole::health,{48,192,152},0,"ammo_slugs");
    r.point(PlacementRole::health,{768,384,152},0,"ammo_cells");
    r.point(PlacementRole::health,{688,-368,24},0,"ammo_bullets");
    r.point(PlacementRole::health,{-688,400,152},0,"ammo_bullets");

    for(auto p:{GridVec3{-736,-640,144},{-544,-560,128},{-736,-352,224},{-704,-96,272},
        {-448,-32,256},{-672,208,256},{-384,176,160},{-464,-432,176}}) r.light(p,360,{1000,840,640});
    for(auto p:{GridVec3{-144,-608,160},{16,-592,152},{192,-480,144},{32,-320,88},
        {32,-320,264},{-144,-160,256},{16,-144,96},{112,16,216},{256,16,392},
        {256,-224,336},{-112,16,256}}) r.light(p,360,{760,900,1000});
    for(auto p:{GridVec3{-144,240,264},{80,272,264},{80,464,264},{48,640,264},{240,592,280}})r.light(p,360);
    for(auto p:{GridVec3{608,-144,136},{448,-144,88},{448,144,256},{608,176,128},
        {768,288,264},{768,512,264},{592,640,88},{608,640,264},{448,464,256}})r.light(p,360);
    for(auto p:{GridVec3{448,-640,160},{752,-512,160},{480,-368,160},{704,-368,160}})r.light(p,340,{1000,880,720});
    for(auto p:{GridVec3{-752,400,264},{-720,608,264},{-448,624,264},{-432,400,264}})r.light(p,320,{800,960,1000});
    r.light({-576,-192,112},280,{1000,840,640});
    for(const auto &c:r.level().connections)for(const auto &b:c.passage_volumes){auto p=b.center();p.z=b.max.z-24;r.light(p,360);}
    r.finish(std::filesystem::absolute(argv[1]));
    if(r.result.quality->score.warnings)throw std::runtime_error("Ravel advisory findings require review");
    return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
