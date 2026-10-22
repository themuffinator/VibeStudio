// Browser QA for an analytical recipe diagram. No engine rendering or user input.
// Usage: node tests/recipe_preview_check.cjs <wrapped-preview.html> <evidence-dir>
// Install Playwright or expose an existing installation with NODE_PATH.
const { chromium } = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { createHash } = require('node:crypto');

(async () => {
  const [previewArgument, outputArgument] = process.argv.slice(2);
  if (!previewArgument || !outputArgument) throw new Error('Expected preview HTML and evidence directory.');
  const preview = path.resolve(previewArgument);
  const output = path.resolve(outputArgument);
  fs.mkdirSync(output, { recursive: true });
  const browser = await chromium.launch({ channel: process.env.QMG_BROWSER_CHANNEL || 'msedge', headless: true });
  const page = await browser.newPage({ viewport: { width: 768, height: 1050 }, colorScheme: 'dark' });
  const errors = [];
  const states = [];
  const screenshots = [];
  page.on('pageerror', error => errors.push(error.message));
  try {
    await page.goto(pathToFileURL(preview).href);
    const frame = page.frames().find(value => value !== page.mainFrame());
    if (!frame) throw new Error('Expected skill preview wrapper with a sandboxed iframe.');
    await frame.waitForSelector('#recipe-layout-v2 [data-layout-stage] svg');
    const mapName = await frame.evaluate(() => JSON.parse(document.querySelector('#recipe-layout-v2-data').textContent).map_name);
    if (!/^[a-z][a-z0-9_-]*$/.test(mapName)) throw new Error('Invalid preview map filename identifier.');
    const fragmentName = `${mapName.replace(/^qmg_/, '')}-layout.html`;
    const floors = await frame.evaluate(() => [...document.querySelector('[data-layout-floor]').options].map(value => value.value));
    for (const width of [768, 392, 352]) for (const theme of ['dark', 'light']) {
      await page.setViewportSize({ width, height: 1050 });
      await page.emulateMedia({ colorScheme: theme });
      for (const floor of floors) {
        // Update only this isolated diagram's own DOM state; never dispatch input events.
        const state = await frame.evaluate(floor => {
          const select = document.querySelector('[data-layout-floor]');
          select.value = floor;
          select.dispatchEvent(new Event('change'));
          const svg = document.querySelector('[data-layout-stage] svg');
          const data = JSON.parse(document.querySelector('#recipe-layout-v2-data').textContent);
          const levels = [...svg.querySelectorAll('g[data-level]')].map(g => g.dataset.level);
          const labels = [...svg.querySelectorAll('text')].map(text => {
            const b = text.getBoundingClientRect();
            return { text:text.textContent, x:b.x, y:b.y, right:b.right, bottom:b.bottom, fontSize:getComputedStyle(text).fontSize };
          });
          const bounds = svg.getBoundingClientRect();
          const clippedLabels = labels.filter(b => b.x < bounds.x - 1 || b.right > bounds.right + 1 || b.y < bounds.y - 1 || b.bottom > bounds.bottom + 1);
          const overlap = [];
          for (let i = 0; i < labels.length; i++) for (let j = i + 1; j < labels.length; j++) {
            const a = labels[i], b = labels[j];
            if (a.x < b.right && a.right > b.x && a.y < b.bottom && a.bottom > b.y) overlap.push([a.text, b.text]);
          }
          const stairs = [...svg.querySelectorAll('[data-stair-run]')].map(run => ({id:run.dataset.stairRun, low:Number(run.dataset.low), high:Number(run.dataset.high)}));
          const nearestLevel = z => data.levels.reduce((best, candidate) => Math.abs(candidate - z) < Math.abs(best - z) ? candidate : best);
          const expectedMechanisms = (data.mechanisms || []).filter(mechanism => floor === 'stacked' ||
            [mechanism.from,mechanism.to].some(point => nearestLevel(point[2]) === Number(floor)));
          const mechanisms = [...svg.querySelectorAll('[data-mechanism-kind]')].map(group => ({
            id:group.dataset.mechanism, kind:group.dataset.mechanismKind,
            fromZ:Number(group.dataset.fromZ), toZ:Number(group.dataset.toZ),
            stations:[...group.querySelectorAll('[data-mechanism-station]')].map(station => ({
              endpoint:station.dataset.mechanismStation, level:Number(station.dataset.stationLevel), z:Number(station.dataset.stationZ)
            })),
            links:[...group.querySelectorAll('[data-mechanism-link]')].map(link => ({
              direction:link.dataset.direction, start:link.getAttribute('marker-start'), end:link.getAttribute('marker-end')
            })),
            footprints:group.querySelectorAll('[data-lift-footprint]').length
          }));
          const mechanismIssues = [];
          const expectedDrops = (data.drops || []).filter(drop => floor === 'stacked' ||
            [drop.from[2],drop.to[2]].includes(Number(floor)));
          const dropIssues = [];
          const drops = [...svg.querySelectorAll('[data-drop]')].map(group => ({
            id:group.dataset.drop, fromZ:Number(group.dataset.fromZ), toZ:Number(group.dataset.toZ),
            stations:[...group.querySelectorAll('[data-drop-station]')].map(station => ({
              endpoint:station.dataset.dropStation, level:Number(station.dataset.stationLevel), z:Number(station.dataset.stationZ),
              point:[Number(station.dataset.pointX),Number(station.dataset.pointY)]
            })),
            segments:[...group.querySelectorAll('[data-drop-segment]')].map(segment => ({
              kind:segment.dataset.dropSegment, tag:segment.tagName, from:segment.dataset.segmentFrom,to:segment.dataset.segmentTo,
              direction:segment.dataset.direction,start:segment.getAttribute('marker-start'),end:segment.getAttribute('marker-end'),
              a:[Number(segment.getAttribute('x1')),Number(segment.getAttribute('y1'))],
              b:[Number(segment.getAttribute('x2')),Number(segment.getAttribute('y2'))]
            }))
          }));
          const expectedRamps = (data.ramps || []).filter(ramp => floor === 'stacked' ||
            [ramp.base_floor,ramp.to[2]].includes(Number(floor)));
          const rampIssues = [];
          const ramps = [...svg.querySelectorAll('[data-ramp]')].map(group => ({
            id:group.dataset.ramp, solidId:group.dataset.solidId,
            low:Number(group.dataset.low),high:Number(group.dataset.high),
            vertices:[...(group.querySelector('[data-ramp-surface]')?.points || [])].map(point => [point.x,point.y]),
            arrows:group.querySelectorAll('[data-ramp-up-arrow][marker-end]').length
          }));
          const sortedLevels = [...data.levels, ...(data.local_floor_levels || []).map(value => value.height)].sort((a,b)=>a-b);
          const levelPosition = z => {
            if (sortedLevels.length === 1) return 0;
            let index=sortedLevels.findIndex(level=>level>=z);
            if (index===-1) index=sortedLevels.length-1;
            if (index===0) return (z-sortedLevels[0])/(sortedLevels[1]-sortedLevels[0]);
            return index-1+(z-sortedLevels[index-1])/(sortedLevels[index]-sortedLevels[index-1]);
          };
          for (const expected of expectedDrops) {
            const actual = drops.find(drop => drop.id === String(expected.id));
            if (!actual) {dropIssues.push(`Missing drop ${expected.id}`);continue;}
            if (actual.fromZ !== expected.from[2] || actual.toZ !== expected.to[2]) dropIssues.push(`Wrong drop heights ${expected.id}`);
            const endpoints = ['from','departure','to'].filter(endpoint => floor === 'stacked' || expected[endpoint][2] === Number(floor));
            if (actual.stations.length !== endpoints.length) dropIssues.push(`Wrong drop station count ${expected.id}`);
            const base = actual.stations.find(station => station.endpoint === endpoints[0]);
            for (const endpoint of endpoints) {
              const station = actual.stations.find(value => value.endpoint === endpoint);
              if (!station || station.z !== expected[endpoint][2] || station.level !== expected[endpoint][2]) {
                dropIssues.push(`Missing physical drop station ${expected.id}:${endpoint}`);continue;
              }
              if (endpoint !== 'departure' && ![...svg.querySelectorAll('[data-label-drop]')].some(label =>
                label.dataset.labelDrop === String(expected.id) && label.dataset.dropLabelEndpoint === endpoint))
                dropIssues.push(`Unlabelled drop endpoint ${expected.id}:${endpoint}`);
              if (base) {
                const a = expected[endpoints[0]], b = expected[endpoint], dx = b[0]-a[0], dy = b[1]-a[1];
                const scale = Number(svg.dataset.worldScale), step = Number(svg.dataset.levelStep);
                const projected = [(floor === 'stacked' ? .72*(dx-dy) : dx)*scale,
                  (floor === 'stacked' ? -.25*(dx+dy)-(levelPosition(b[2])-levelPosition(a[2]))*step : -dy)*scale];
                if (projected.some((value,i) => Math.abs(station.point[i]-base.point[i]-value) > .02))
                  dropIssues.push(`Drop ${expected.id} does not preserve actual XYZ projection`);
              }
            }
            const expectedSegments = floor === 'stacked' ? ['departure','descent'] :
              Number(floor) === expected.from[2] ? ['departure'] : [];
            if (actual.segments.length !== expectedSegments.length || expectedSegments.some(kind => !actual.segments.some(s => s.kind === kind)))
              dropIssues.push(`Wrong drop legs for selected floor ${expected.id}`);
            for (const segment of actual.segments) {
              const a = actual.stations.find(station => station.endpoint === segment.from);
              const b = actual.stations.find(station => station.endpoint === segment.to);
              if (segment.tag !== 'line' || segment.direction !== 'one-way' || segment.start || !segment.end || !a || !b ||
                segment.a.some((v,i) => Math.abs(v-a.point[i])>.02) || segment.b.some((v,i) => Math.abs(v-b.point[i])>.02))
                dropIssues.push(`Drop ${expected.id} must use exact directional straight legs, not a ballistic curve`);
            }
          }
          if (drops.length !== expectedDrops.length) dropIssues.push('Unexpected drop on selected floor');
          if (expectedDrops.length) {
            const detail = document.querySelector('[data-layout-detail]').textContent;
            const probed = data.native_drop_evidence?.world_only === true && data.native_drop_evidence?.gameplay_tested === false &&
              data.native_drop_evidence?.diagram_is_native_trajectory === false && expectedDrops.every(drop =>
                data.native_drop_evidence.drops.some(value => value.id === drop.id && value.probe_count === 2));
            const scope = probed ? 'Native world movement probed; gameplay untested' : 'Runtime untested';
            if (!detail.includes('candidate descent') || !detail.includes(scope) ||
                (probed && detail.includes('Runtime untested'))) dropIssues.push('Missing or contradictory candidate/runtime scope label');
            if (![...svg.querySelectorAll('desc')].some(text => text.textContent.includes('not a native momentum trajectory')))
              dropIssues.push('Drop diagram lacks geometric trajectory distinction');
            const checked = expectedDrops.every(drop => data.drop_evidence?.drops.some(value => value.id === drop.id && value.ordinary_return_verified));
            if (!detail.includes(checked ? 'Ordinary return graph checked' : 'Ordinary return audit pending')) dropIssues.push('Incorrect return evidence label');
          }
          for (const expected of expectedRamps) {
            const actual=ramps.find(ramp=>ramp.id===String(expected.id));
            if (!actual || actual.vertices.length!==4) { rampIssues.push(`Missing ramp quad ${expected.id}`);continue; }
            if (actual.low!==expected.from[2] || actual.high!==expected.to[2] || actual.arrows!==1)
              rampIssues.push(`Wrong ramp elevation/arrow ${expected.id}`);
            if (svg.querySelector(`[data-blocker-layer] [data-solid-id="${expected.solid_id}"]`))
              rampIssues.push(`Ramp ${expected.id} also rendered as a solid AABB shadow`);
            if (![...svg.querySelectorAll('[data-label-ramp]')].some(label=>label.dataset.labelRamp===String(expected.id)))
              rampIssues.push(`Unlabelled ramp ${expected.id}`);
            const scale=Number(svg.dataset.worldScale), levelStep=Number(svg.dataset.levelStep), origin=expected.surface[0];
            // Compare projected edge vectors with actual XYZ deltas. A ramp flattened
            // to its max-Z AABB top fails this even if it retains correct metadata.
            for (let i=1;i<4;i++) {
              const point=expected.surface[i], dx=point[0]-origin[0],dy=point[1]-origin[1];
              const expectedX=(floor==='stacked' ? .72*(dx-dy) : dx)*scale;
              const expectedY=(floor==='stacked' ? -.25*(dx+dy)-(levelPosition(point[2])-levelPosition(origin[2]))*levelStep : -dy)*scale;
              if (Math.abs(actual.vertices[i][0]-actual.vertices[0][0]-expectedX)>.02 ||
                  Math.abs(actual.vertices[i][1]-actual.vertices[0][1]-expectedY)>.02)
                rampIssues.push(`Ramp ${expected.id} does not project its actual sloped surface`);
            }
          }
          if (ramps.length!==expectedRamps.length) rampIssues.push('Unexpected ramp on selected floor');
          const expectedLiquids = (data.liquids || []).flatMap(liquid => sortedLevels
            .filter(z => (floor === 'stacked' || z === Number(floor)) && liquid.bounds[0][2] <= z && liquid.bounds[1][2] > z)
            .map(z => ({...liquid,level:z})));
          const liquids = [...svg.querySelectorAll('[data-liquid-id]')].map(shape => ({
            id:shape.dataset.liquidId,level:Number(shape.dataset.liquidLevel),
            surface:Number(shape.dataset.liquidSurface),contents:shape.dataset.liquidContents
          }));
          const liquidIssues = [];
          for (const marker of svg.querySelectorAll('[data-anchor][data-role="major_item"]'))
            if (!svg.querySelector(`[data-label-anchor="${marker.dataset.anchor}"]`))
              liquidIssues.push('Visible major item lacks its label');
          if (liquids.length !== expectedLiquids.length) liquidIssues.push('Wrong liquid count on selected floor');
          for (const expected of expectedLiquids) {
            const actual = liquids.find(l => l.id === String(expected.id) && l.level === expected.level);
            if (!actual || actual.surface !== expected.bounds[1][2] || actual.contents !== expected.contents)
              liquidIssues.push(`Wrong liquid geometry metadata ${expected.id}`);
            if (svg.querySelector(`[data-blocker-layer] [data-solid-id="${expected.id}"]`))
              liquidIssues.push(`Liquid ${expected.id} incorrectly rendered as solid cover`);
          }
          if (expectedLiquids.length || (data.auxiliary_stair_runs || []).length) {
            // Independently recover only diagram translation from a real room
            // corner, then verify water and wet tread heights from authored XYZ.
            const shownZ = floor === 'stacked' ? sortedLevels[0] : Number(floor);
            const room = data.rooms.find(r => r.bounds[0][2] === shownZ);
            const base = room && svg.querySelector(`[data-level="${shownZ}"] [data-floor-surface][data-room="${room.id}"]`);
            if (!base) liquidIssues.push('No geometric reference for local-depth projection');
            else {
              const scale=Number(svg.dataset.worldScale), step=Number(svg.dataset.levelStep);
              const raw=(p,z)=>floor==='stacked' ? [.72*(p[0]-p[1])*scale,(-.25*(p[0]+p[1])-levelPosition(z)*step)*scale] : [p[0]*scale,-p[1]*scale];
              const baseRaw=raw(room.bounds[0],shownZ);
              const offset=[base.points[0].x-baseRaw[0],base.points[0].y-baseRaw[1]];
              const corners=b=>[[b[0][0],b[0][1]],[b[1][0],b[0][1]],[b[1][0],b[1][1]],[b[0][0],b[1][1]]];
              const check=(shape,b,z,label)=>{
                if (!shape || shape.points.length!==4) {liquidIssues.push(`Missing ${label} polygon`);return;}
                corners(b).forEach((p,i)=>{
                  const expected=raw(p,floor==='stacked'?z:shownZ).map((v,axis)=>v+offset[axis]);
                  if (Math.abs(shape.points[i].x-expected[0])>.002 || Math.abs(shape.points[i].y-expected[1])>.002)
                    liquidIssues.push(`Incorrect actual XYZ projection for ${label}`);
                });
              };
              for (const expected of expectedLiquids)
                check(svg.querySelector(`[data-liquid-id="${expected.id}"][data-liquid-level="${expected.level}"]`),expected.bounds,expected.bounds[1][2],'water surface');
              for (const ceiling of data.overhead_obstacles || []) {
                const visible=floor==='stacked'||ceiling.context===shownZ;
                const actual=svg.querySelector(`[data-overhead-context="${ceiling.context}"]`);
                if (visible) check(actual,ceiling.bounds,ceiling.bounds[0][2],'submerged roof underside');
                else if(actual)liquidIssues.push('Submerged roof shown on wrong floor');
              }
              for (const run of data.auxiliary_stair_runs || []) {
                const visible=floor==='stacked'||[run.steps[0][0][2],run.steps.at(-1)[1][2]].includes(shownZ);
                const group=svg.querySelector(`[data-stair-run="${run.id}"]`);
                if (!visible) {if(group)liquidIssues.push('Wet stairs on wrong floor');continue;}
                const polygons=group ? [...group.querySelectorAll('polygon')] : [];
                if(polygons.length!==run.steps.length)liquidIssues.push('Wet stair tread count differs');
                run.steps.forEach((b,i)=>check(polygons[i],b,b[1][2],`wet tread ${run.id}:${i}`));
              }
            }
          }
          if ((data.liquids || []).length && document.querySelector('[data-key-liquid]').hidden)
            liquidIssues.push('Missing liquid legend');
          for (const expected of expectedMechanisms) {
            const actual = mechanisms.find(value => value.id === String(expected.id));
            if (!actual || actual.kind !== expected.kind) { mechanismIssues.push(`Missing mechanism ${expected.id}`); continue; }
            const endpoints = ['from','to'].filter(endpoint => floor === 'stacked' || nearestLevel(expected[endpoint][2]) === Number(floor));
            if (actual.stations.length !== endpoints.length) mechanismIssues.push(`Wrong station count for ${expected.id}`);
            for (const endpoint of endpoints) {
              const station = actual.stations.find(value => value.endpoint === endpoint);
              if (!station || station.z !== expected[endpoint][2] || station.level !== nearestLevel(expected[endpoint][2]))
                mechanismIssues.push(`Wrong physical station or declared floor for ${expected.id}:${endpoint}`);
            }
            const shouldLink = endpoints.length === 2;
            if (actual.links.length !== Number(shouldLink)) mechanismIssues.push(`Wrong link visibility for ${expected.id}`);
            for (const link of actual.links) {
              if (expected.kind === 'lift' && (link.direction !== 'bidirectional-stateful' || !link.start || !link.end))
                mechanismIssues.push(`Lift ${expected.id} must show both directions and stateful travel`);
              if (expected.kind !== 'lift' && (link.direction !== 'one-way' || link.start || !link.end))
                mechanismIssues.push(`Directed mechanism ${expected.id} has incorrect arrows`);
            }
            if (expected.kind === 'lift' && actual.footprints !== endpoints.length) mechanismIssues.push(`Missing lift footprint for ${expected.id}`);
            if (![...svg.querySelectorAll('[data-label-mechanism]')].some(label => label.dataset.labelMechanism === String(expected.id)))
              mechanismIssues.push(`Unlabelled mechanism ${expected.id}`);
          }
          if (mechanisms.length !== expectedMechanisms.length) mechanismIssues.push('Unexpected mechanism on selected floor');
          const footprints = [...svg.querySelectorAll('[data-footprint-vertices]')].map(polygon => ({
            id:polygon.dataset.solidId, expected:Number(polygon.dataset.footprintVertices), actual:polygon.points.numberOfItems
          }));
          if (footprints.some(value => value.expected !== value.actual || value.actual < 3)) mechanismIssues.push('Faceted solid replaced by incorrect polygon');
          const invalidGeometry = [...svg.querySelectorAll('polygon,line,path,circle,rect')].filter(element =>
            [...element.attributes].some(attribute => /(?:NaN|Infinity)/.test(attribute.value))).length;
          const clippedGeometry = [...svg.querySelectorAll('polygon,circle,rect')].filter(element => {
            if (element.closest('defs')) return false;
            const b = element.getBoundingClientRect();
            return b.x < bounds.x - 1 || b.right > bounds.right + 1 || b.y < bounds.y - 1 || b.bottom > bounds.bottom + 1;
          }).length;
          const planIssues = [];
          let plan = null;
          if (floor !== 'stacked') {
            const scale = Number(svg.dataset.worldScale);
            const outline = svg.querySelector('[data-room-outline]');
            const room = data.rooms.find(room => String(room.id) === outline?.dataset.roomOutline);
            if (!room || svg.dataset.projection !== 'orthographic-xy' || !(scale > 0)) {
              planIssues.push('Plan needs a room outline and explicit XY projection');
            } else {
              const actual = [...outline.points].map(point => [point.x,point.y]);
              const [lo,hi] = room.bounds;
              const world = [[lo[0],lo[1]],[hi[0],lo[1]],[hi[0],hi[1]],[lo[0],hi[1]]];
              const origin = [actual[0][0]-world[0][0]*scale,actual[0][1]+world[0][1]*scale];
              if (actual.length !== 4 || world.some((p,i) => Math.abs(actual[i][0]-(origin[0]+p[0]*scale))>.001 ||
                  Math.abs(actual[i][1]-(origin[1]-p[1]*scale))>.001)) planIssues.push('Room plan is not orthographic XY with +Y upward');
              plan = {scale,origin};
            }
            const bar = svg.querySelector('[data-plan-scale] [data-scale-bar]');
            const units = Number(svg.querySelector('[data-plan-scale]')?.dataset.planScale);
            if (!bar || !(units > 0) || Math.abs(Number(bar.getAttribute('x2'))-Number(bar.getAttribute('x1'))-units*scale)>.001 ||
                bar.getAttribute('y1') !== bar.getAttribute('y2')) planIssues.push('Scale bar does not match projected map units');
          }
          return { floor, polygons:svg.querySelectorAll('polygon').length,
            visibleSurfaces:svg.querySelectorAll('[data-floor-surface],[data-route]').length,
            anchors:svg.querySelectorAll('[data-anchor]').length,
            expectedAllAnchors:data.anchors.filter(a => ['spawn','weapon','armor','major_item'].includes(a.role)).length,
            levels, stairs, expectedRuns:data.stair_runs.length + (data.auxiliary_stair_runs || []).length, arrowCount:svg.querySelectorAll('[data-up-arrow]').length,
            invalidGeometry, clippedGeometry, mechanisms, mechanismIssues, ramps, rampIssues, drops, dropIssues, liquids, liquidIssues, footprints, plan, planIssues,
            labelCount:labels.length, clippedLabels, overlappingLabels:overlap,
            minLabelSize:Math.min(...labels.map(label => parseFloat(label.fontSize))),
            contentWidth:document.documentElement.clientWidth, scrollWidth:document.documentElement.scrollWidth,
            detail:document.querySelector('[data-layout-detail]').textContent };
        }, floor);
        if (!state.visibleSurfaces || state.invalidGeometry || state.clippedGeometry || state.mechanismIssues.length || state.rampIssues.length || state.dropIssues.length || state.liquidIssues.length || state.planIssues.length || state.scrollWidth > state.contentWidth + 1 || state.clippedLabels.length || state.overlappingLabels.length || state.minLabelSize < 11)
          throw new Error(`Layout QA failed: ${JSON.stringify({width,theme,...state})}`);
        if (floor !== 'stacked' && (state.levels.length !== 1 || state.levels[0] !== floor)) throw new Error('Wrong floor is visible.');
        if (floor === 'stacked' && state.anchors !== state.expectedAllAnchors) throw new Error('An authored gameplay marker is missing, including local prop landings.');
        if (state.arrowCount !== state.stairs.length || state.stairs.some(run => !Number.isFinite(run.low) || !Number.isFinite(run.high) || run.high <= run.low)) throw new Error('Invalid stair direction metadata.');
        if (floor === 'stacked' && state.stairs.length !== state.expectedRuns) throw new Error('Missing stair runs in stacked view.');
        const priorPlan = states.find(row => row.width === width && row.theme === theme && row.plan)?.plan;
        if (state.plan && priorPlan && (Math.abs(state.plan.scale-priorPlan.scale)>.000001 ||
            state.plan.origin.some((v,i) => Math.abs(v-priorPlan.origin[i])>.001)))
          throw new Error('Changing floors shifted or rescaled the common XY plan.');
        states.push({ width, theme, ...state });
        const captureName = width === 768 && theme === 'dark' ? (floor === 'stacked' ? 'overview.png' : `floor-${floor}.png`) :
          width === 352 && theme === 'light' && floor === 'stacked' ? 'overview-mobile-light.png' : null;
        if (captureName) {
          const filename = path.join(output,captureName);
          const box=await frame.locator('#recipe-layout-v2').boundingBox();
          const viewport=page.viewportSize();
          const x=Math.max(0,box.x-16),y=Math.max(0,box.y-16);
          await page.screenshot({path:filename,clip:{x,y,
            width:Math.min(viewport.width-x,box.width+32),height:Math.min(viewport.height-y,box.height+32)}});
          screenshots.push({path:filename,width,theme,floor});
        }
      }
    }
    if (errors.length) throw new Error(errors.join('\n'));
    const hashFile = filename => createHash('sha256').update(fs.readFileSync(filename)).digest('hex');
    const inputHashes = Object.fromEntries(['layout.json','compiled-analysis.json','verification.json',fragmentName,`${mapName}.bsp`,'native-routes/summary.json'].filter(name => fs.existsSync(path.join(path.dirname(preview),name)))
      .map(name => [name,hashFile(path.join(path.dirname(preview),name))]));
    fs.writeFileSync(path.join(output, 'preview-checks.json'), JSON.stringify({ purpose:'Analytical diagram QA; no game rendering or playtesting.',
      preview, previewSha256:hashFile(preview), fragmentSha256:inputHashes[fragmentName], inputHashes,
      screenshots:screenshots.map(capture=>({...capture,sha256:hashFile(capture.path)})), states, errors }, null, 2) + '\n');
    console.log(`Verified ${states.length} diagram states across three widths, both themes and every floor.`);
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
