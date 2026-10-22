// Original outdoor-spine and optional submerged-return experiment. No reference
// geometry is copied; the culvert is a separate commitment from the dry loop.
#include "recipe_support.hpp"
using namespace qmg;

int main(int argc, char **argv) try {
    if (argc != 2)
        throw std::runtime_error("usage: qmg_lockyard_recipe <output directory>");
    recipes::Recipe r("qmg_lockyard", "Lockyard",
                      "Compact asymmetric Quake II outdoor lock courtyard with partial galleries, "
                      "offset loading cover, bent dry perimeter circulation and optional deep water culvert",
                      20260918, 6);
    r.floor_levels = {0, 128};
    r.require_ordinary_routes = r.require_no_drop_routes = true;
    r.room("Lock Court", {{-384,-224,0},{384,512,512}});
    r.room("Boiler", {{-1008,-224,0},{-432,768,384}});
    r.room("Crane", {{-384,560,128},{1008,960,448}});
    r.room("Lockhouse", {{432,-224,0},{1008,512,384}});
    r.room("Winding", {{-1008,-768,0},{-432,-272,352}});
    r.room("Culvert", {{-384,-768,-128},{1008,-272,192}});
    r.sky_rooms.insert(RoomId{1});
    r.worldspawn_properties["sky"]="unit1_";
    r.worldspawn_properties["_sunlight"]="180";
    r.worldspawn_properties["_sun_mangle"]="35 -55 0";
    r.material_overrides["e1u1/water1"]="e3u2/water2";
    r.quake2_material_metadata_overrides["e3u2/water2"]={32,24,0};

    r.connection(1,2,160,{{{-432,-96,0},{-384,64,192}}});
    r.connection(1,2,144,{{{-432,352,128},{-384,496,320}}});
    r.connection(1,3,160,{{{64,512,128},{224,560,320}}});
    r.connection(1,4,160,{{{384,32,0},{432,192,192}}});
    r.connection(2,3,160,{{{-432,608,128},{-384,768,320}}});
    r.connection(2,5,160,{{{-880,-272,0},{-720,-224,192}}});
    r.connection(5,6,160,{{{-432,-640,0},{-384,-480,192}}});
    r.connection(6,4,160,{{{832,-272,0},{992,-224,192}}});
    r.connection(3,4,160,{{{672,512,128},{832,560,320}}});
    r.connection(1,6,144,{{{-384,-272,0},{-240,-224,192}}});

    // The court is open above. Two sides have usable galleries; its other two
    // approaches remain open sky volumes instead of a duplicated upper circuit.
    r.deck(-384,352,384,512,128);
    r.deck(224,-96,384,352,128);
    r.stairs(-304,64,0,1,8);
    r.solid({{-80,208,0},{96,304,128}});
    r.solid({{240,368,128},{304,432,240}});
    r.prism({{-384,512},{-384,448},{-320,512}},128,320);
    r.solid({{208,-192,0},{272,-128,96}});

    r.deck(-592,-224,-432,768,128);
    r.deck(-1008,608,-592,768,128);
    r.stairs(-880,368,0,0,8);
    r.solid({{-880,32,0},{-752,160,176}});
    r.solid({{-592,96,128},{-528,224,240}});
    r.solid({{-800,656,128},{-736,752,256}});

    // Crane is an upper cross-route with two offsets, not a central hub.
    r.solid({{-16,704,128},{144,960,448}});
    r.solid({{544,560,128},{608,736,304}});
    r.prism({{912,960},{1008,864},{1008,960}},128,448);

    r.deck(432,352,1008,512,128);
    r.deck(848,-64,1008,352,128);
    r.stairs(672,64,0,1,8);
    r.solid({{528,-144,0},{624,-48,128}});
    r.solid({{848,352,128},{912,416,256}});

    r.deck(-1008,-432,-432,-272,128);
    r.stairs(-960,-720,0,1,8);
    r.solid({{-656,-672,0},{-560,-560,128}});
    r.solid({{-704,-368,128},{-640,-272,240}});

    // Optional submerged connection. All pickups and spawns remain dry; the
    // two dry stations are also connected through Winding/Boiler/Court/Lockhouse.
    r.deck(-384,-768,-224,-272,0);
    r.deck(736,-768,1008,-272,0);
    std::vector<GridBounds> descent,ascent;
    for(int i=0;i<8;++i){
        GridBounds down{{-224+i*36,-656,-128},{-188+i*36,-464,-16*i}};
        GridBounds up{{448+i*36,-656,-128},{484+i*36,-464,-112+16*i}};
        descent.push_back(down);ascent.push_back(up);
        r.solid(down,MaterialRole::stair,BrushPurpose::structural);
        r.solid(up,MaterialRole::stair,BrushPurpose::structural);
    }
    const GridBounds water{{-224,-736,-128},{736,-304,-16}};
    r.assemble({LiquidSpec{{1},water,BrushContents::water,BrushOwnership::structural}});
    // A short submerged lintel makes this connection commit to the deeper
    // movement volume. Both long approach/exit sections have clear air above.
    const GridBounds submerged_ceiling{{256,-768,-40},{352,-272,192}};
    r.solid(submerged_ceiling);

    r.spawn({-128,464,24},0);
    r.spawn({320,464,152},270);
    r.spawn({-944,-128,24},0);
    r.spawn({-688,688,152},270);
    r.spawn({-256,880,152},270);
    r.spawn({928,688,152},180);
    r.spawn({496,-128,24},180);
    r.spawn({-496,-688,24},90);

    r.point(PlacementRole::weapon,{-64,-64,24},4);
    r.point(PlacementRole::weapon,{-784,-144,24},0,"weapon_supershotgun");
    r.point(PlacementRole::weapon,{-496,304,152},2);
    r.point(PlacementRole::weapon,{400,832,152},6);
    r.point(PlacementRole::weapon,{-208,656,152},3);
    r.point(PlacementRole::weapon,{928,240,152},5);
    r.point(PlacementRole::weapon,{-832,-336,152},0);
    r.point(PlacementRole::armor,{288,128,152});
    r.point(PlacementRole::armor,{-928,560,24});
    r.point(PlacementRole::health,{96,432,24},0,"item_health_mega");
    r.point(PlacementRole::health,{-512,-352,152},0,"item_quad");
    for(auto p:{GridVec3{-160,288,24},{320,-32,24},{-688,224,24},{-528,560,152},
                {752,848,152},{752,-144,24},{-784,-688,24},{848,-400,24}})
        r.point(PlacementRole::health,p);
    r.point(PlacementRole::health,{-128,-64,24},0,"ammo_rockets");
    r.point(PlacementRole::health,{-720,-144,24},0,"ammo_shells");
    r.point(PlacementRole::health,{-496,368,152},0,"ammo_bullets");
    r.point(PlacementRole::health,{464,832,152},0,"ammo_slugs");
    r.point(PlacementRole::health,{-144,656,152},0,"ammo_grenades");
    r.point(PlacementRole::health,{928,304,152},0,"ammo_cells");

    // Keep analytical light probes supported by explicit lights even before a
    // rendered sunlight/readability review. The sky brush remains sealed.
    for(const auto &room:r.level().rooms){
        for(int z:{72,224}) for(int x=room.bounds.min.x+80;x<room.bounds.max.x-32;x+=224)
            for(int y=room.bounds.min.y+80;y<room.bounds.max.y-32;y+=224){
                const GridVec3 p{x,y,z};
                if(!room.bounds.contains(p))continue;
                if(std::any_of(r.level().solids.begin(),r.level().solids.end(),[&](const auto &s){return checked_brush_bounds(s.geometry).contains(p);}))continue;
                r.light(p,room.id.value==6?600:(room.id.value==5?120:(room.id.value==4?220:380)),room.id.value==1?RgbColor{1000,920,760}:RgbColor{800,900,1000});
            }
    }
    for(const auto &c:r.level().connections)for(const auto &b:c.passage_volumes){auto p=b.center();p.z=b.max.z-24;r.light(p,360);}
    const auto output=std::filesystem::absolute(argv[1]);
    r.finish(output);
    if(r.result.quality->score.warnings)throw std::runtime_error("Lockyard advisory findings require review");
    std::ofstream f(output/"features.json",std::ios::binary);
    f << "{\"schema_version\":1,\"map\":\"qmg_lockyard\",\"layout_sha256\":" << std::quoted(r.result.player_navigation->layout_sha256)
      << ",\"main_floors\":[0,128],\"sky_rooms\":[1],\"sky_roof\":[[-384,-224,512],[384,512,536]],\"sky\":\"unit1_\","
         "\"local_wet_bed\":-128,\"water\":";
    recipes::bounds_json(f,water);
    f << ",\"submerged_ceiling\":";
    recipes::bounds_json(f,submerged_ceiling);
    f << ",\"wet_trial\":{\"start_feet\":[-320,-560,0],\"goal_feet\":[832,-560,0],\"bottom_segment\":[[64,-560,-128],[448,-560,-128]],"
         "\"dry_start_turn\":[[-320,-608,0],[-320,-512,0]],\"dry_goal_turn\":[[832,-608,0],[832,-512,0]],\"descent_steps\":[";
    for(std::size_t i=0;i<descent.size();++i){if(i)f<<',';recipes::bounds_json(f,descent[i]);}
    f << "],\"ascent_steps\":[";
    for(std::size_t i=0;i<ascent.size();++i){if(i)f<<',';recipes::bounds_json(f,ascent[i]);}
    f << "],\"native_trial_defaults\":{\"yaw\":0,\"pitch\":0,\"msec\":16,\"forward_run\":400,\"forward_walk\":200,\"side\":0,\"up\":0,\"buttons\":0},"
         "\"runtime_verified\":false},\"flat_segments\":["
         "{\"label\":\"Court north lower crossing\",\"from\":[-144,432,0],\"to\":[160,432,0]},"
         "{\"label\":\"Court north gallery crossing\",\"from\":[-144,432,128],\"to\":[160,432,128]},"
         "{\"label\":\"Court east dry approach\",\"from\":[160,0,0],\"to\":[160,320,0]},"
         "{\"label\":\"Culvert dry west turn\",\"from\":[-320,-608,0],\"to\":[-320,-512,0]},"
         "{\"label\":\"Culvert dry east turn\",\"from\":[832,-608,0],\"to\":[832,-512,0]}]}";
    if(!f)throw std::runtime_error("feature export failed");
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
