// Isolated analytical browser QA. No user input, game process or OS capture.
// node tools/ql_atlas/preview_check.cjs <wrapped-index.html> <output-dir>
const {chromium}=require('playwright');
const fs=require('node:fs');
const path=require('node:path');
const {pathToFileURL}=require('node:url');
const {createHash}=require('node:crypto');
(async()=>{
  const [previewArg,outputArg]=process.argv.slice(2);
  if(!previewArg||!outputArg)throw new Error('preview and output paths required');
  const preview=path.resolve(previewArg),output=path.resolve(outputArg);
  fs.mkdirSync(output,{recursive:true});
  const browser=await chromium.launch({headless:true,channel:process.env.QMG_BROWSER_CHANNEL||'msedge'});
  const page=await browser.newPage({viewport:{width:768,height:1500},colorScheme:'dark'});
  const errors=[],states=[],screenshots=[],commonScales={};
  page.on('pageerror',e=>errors.push(e.message));
  try{
    await page.goto(pathToFileURL(preview).href);
    const frame=page.frames().find(f=>f!==page.mainFrame());
    if(!frame)throw new Error('expected sandboxed skill wrapper');
    await frame.waitForSelector('#ql-reference-atlas svg');
    const maps=await frame.evaluate(()=>JSON.parse(document.getElementById('ql-reference-atlas-data').textContent).maps.map(m=>({map:m.map,bands:m.bands})));
    for(const width of [768,392,352])for(const theme of ['dark','light']){
      await page.setViewportSize({width,height:1500});await page.emulateMedia({colorScheme:theme});
      for(const map of maps)for(const scale of ['fit','common'])for(const band of ['all',...map.bands.map(String)]){
        const state=await frame.evaluate(({map,scale,band})=>{
          const root=document.getElementById('ql-reference-atlas');
          // Mutate only our diagram's local select state; never inject input.
          for(const [key,value]of [['map',map],['scale',scale],['band',band]]){const select=root.querySelector(`[data-atlas-${key}]`);select.value=value;select.dispatchEvent(new Event('change'));}
          const svg=root.querySelector('svg'),box=svg.getBoundingClientRect();
          const data=JSON.parse(document.getElementById('ql-reference-atlas-data').textContent).maps.find(m=>m.map===map);
          const paths=[...svg.querySelectorAll('[data-surface-band]')];
          const expectedPaths=data.surfaces.filter(g=>band==='all'||g.band===Number(band)).reduce((a,g)=>a+g.polygons.length,0);
          const expectedPoints=data.annotations.filter(p=>band==='all'||p.band_context.band===Number(band));
          const points=[...svg.querySelectorAll('[data-annotation]')];
          const wrongPoints=points.filter(p=>!expectedPoints.some(e=>String(e.id)===p.dataset.annotation&&e.origin[2]===Number(p.dataset.originZ)&&e.band_context.band===Number(p.dataset.contextBand))).length;
          const labels=[...svg.querySelectorAll('text')].map(t=>{const b=t.getBoundingClientRect();return{text:t.textContent,x:b.x,y:b.y,right:b.right,bottom:b.bottom,fontSize:parseFloat(getComputedStyle(t).fontSize)};});
          const clipped=labels.filter(b=>b.x<box.x-1||b.right>box.right+1||b.y<box.y-1||b.bottom>box.bottom+1);
          const overlaps=[];
          for(let i=0;i<labels.length;i++)for(let j=i+1;j<labels.length;j++){const a=labels[i],b=labels[j];if(a.x<b.right&&a.right>b.x&&a.y<b.bottom&&a.bottom>b.y)overlaps.push([a.text,b.text]);}
          const invalid=[...svg.querySelectorAll('*')].filter(el=>[...el.attributes].some(a=>/NaN|Infinity/.test(a.value))).length;
          const geometryClipped=[...svg.querySelectorAll('path,line,circle,rect,polygon')].filter(el=>!el.closest('defs')).filter(el=>{const b=el.getBoundingClientRect();return b.x<box.x-1||b.right>box.right+1||b.y<box.y-1||b.bottom>box.bottom+1;}).length;
          return{map,scale,band,paths:paths.length,expectedPaths,annotations:points.length,expectedAnnotations:expectedPoints.length,wrongPoints,clipped,overlaps,invalid,geometryClipped,
            unitsPerPixel:Number(svg.dataset.unitsPerPixel),minLabelSize:Math.min(...labels.map(t=>t.fontSize)),contentWidth:document.documentElement.clientWidth,scrollWidth:document.documentElement.scrollWidth,
            detail:root.querySelector('[data-atlas-detail]').textContent};
        },{map:map.map,scale,band});
        if(!state.paths||state.paths!==state.expectedPaths||state.annotations!==state.expectedAnnotations||state.wrongPoints||state.clipped.length||state.overlaps.length||state.invalid||state.geometryClipped||state.minLabelSize<11||state.scrollWidth>state.contentWidth+1)
          throw new Error(JSON.stringify({width,theme,...state}));
        if(scale==='common'){
          const key=`${width}:${theme}`;if(commonScales[key]!==undefined&&Math.abs(commonScales[key]-state.unitsPerPixel)>1e-9)throw new Error('common scale differs between maps');commonScales[key]=state.unitsPerPixel;
        }
        states.push({width,theme,...state});
        if(width===768&&theme==='dark'&&(scale==='fit'||band==='all')){
          const directory=path.join(output,'orthographics',map.map);fs.mkdirSync(directory,{recursive:true});
          const filename=path.join(directory,band==='all'?`all-heights-${scale}.png`:`z-${Number(band)*128}-to-${(Number(band)+1)*128}.png`);
          const box=await frame.locator('#ql-reference-atlas').boundingBox();
          await page.screenshot({path:filename,clip:{x:Math.max(0,box.x-16),y:Math.max(0,box.y-16),width:Math.min(width,box.width+32),height:Math.min(1500,box.height+32)}});
          screenshots.push(filename);
        }
        if(width===352&&theme==='light'&&map.map==='aerowalk'&&scale==='fit'&&band==='all'){
          const filename=path.join(output,'mobile-light.png');const box=await frame.locator('#ql-reference-atlas').boundingBox();
          await page.screenshot({path:filename,clip:{x:0,y:Math.max(0,box.y-16),width:352,height:Math.min(1500,box.height+32)}});screenshots.push(filename);
        }
      }
    }
    if(errors.length)throw new Error(errors.join('\n'));
    const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    const inputs=['atlas.json','ql-reference-atlas.html','index.html',...maps.map(m=>`maps/${m.map}.json`)];
    fs.writeFileSync(path.join(output,'preview-checks.json'),JSON.stringify({purpose:'Analytical orthographic diagrams, not engine render/capture/navigation',states,errors,commonScales,inputHashes:Object.fromEntries(inputs.map(name=>[name,hash(path.join(path.dirname(preview),name))])),screenshots:screenshots.map(file=>({path:file,sha256:hash(file)}))},null,2)+'\n');
    console.log(`Verified ${states.length} atlas states; ${screenshots.length} PNGs; common scale held for all maps.`);
  }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
