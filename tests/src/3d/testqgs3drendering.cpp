/***************************************************************************
  testqgs3drendering.cpp
  --------------------------------------
  Date                 : July 2018
  Copyright            : (C) 2018 by Martin Dobias
  Email                : wonder dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgstest.h"

#include "qgslinestring.h"
#include "qgsmaplayerstylemanager.h"
#include "qgsmapthemecollection.h"
#include "qgsproject.h"
#include "qgsrasterlayer.h"
#include "qgsrastershader.h"
#include "qgssinglebandpseudocolorrenderer.h"
#include "qgsvectorlayer.h"
#include "qgsapplication.h"
#include "qgs3d.h"
#include "qgs3drendercontext.h"

#include "qgs3dmapscene.h"
#include "qgs3dmapsettings.h"
#include "qgs3dutils.h"
#include "qgscameracontroller.h"
#include "qgsflatterrainsettings.h"
#include "qgsdemterrainsettings.h"
#include "qgsflatterraingenerator.h"
#include "qgsline3dsymbol.h"
#include "qgsoffscreen3dengine.h"
#include "qgsframegraph.h"
#include "qgspolygon3dsymbol.h"
#include "qgsrulebased3drenderer.h"
#include "qgsvectorlayer3drenderer.h"
#include "qgspoint3dsymbol.h"
#include "qgssymbollayer.h"
#include "qgsmarkersymbollayer.h"
#include "qgssymbol.h"
#include "qgssinglesymbolrenderer.h"
#include "qgsfillsymbollayer.h"
#include "qgssimplelinematerialsettings.h"
#include "qgsfillsymbol.h"
#include "qgsmarkersymbol.h"
#include "qgsgoochmaterialsettings.h"
#include "qgs3dsceneexporter.h"
#include "qgsdirectionallightsettings.h"
#include "qgsmetalroughmaterialsettings.h"
#include "qgspointlightsettings.h"
#include "qgsphongtexturedmaterialsettings.h"
#include "qgsdemterraingenerator.h"

#include <QFileInfo>
#include <QDir>
#include <QSignalSpy>

class TestQgs3DRendering : public QgsTest
{
    Q_OBJECT

  public:
    TestQgs3DRendering()
      : QgsTest( QStringLiteral( "3D Rendering Tests" ), QStringLiteral( "3d" ) )
    {}

  private slots:
    void initTestCase();    // will be called before the first testfunction is executed.
    void cleanupTestCase(); // will be called after the last testfunction was executed.
    void testLights();
    void testFlatTerrain();
    void testDemTerrain();
    void testTerrainShading();
    void testEpsg4978LineRendering();
    void testExtrudedPolygons();
    void testExtrudedPolygonsClipping();
    void testPhongShading();
    void testExtrudedPolygonsTexturedPhong();
    void testExtrudedPolygonsDataDefinedPhong();
    void testExtrudedPolygonsDataDefinedPhongClipping();
    void testExtrudedPolygonsDataDefinedGooch();
    void testExtrudedPolygonsDataDefinedGoochClipping();
    void testExtrudedPolygonsGoochShading();
    void testExtrudedPolygonsMetalRoughShading();
    void testPolygonsEdges();
    void testLineRendering();
    void testLineRenderingClipping();
    void testLineRenderingCurved();
    void testLineRenderingDataDefinedColors();
    void testBufferedLineRendering();
    void testBufferedLineRenderingClipping();
    void testBufferedLineRenderingWidth();
    void testMapTheme();
    void testRuleBasedRenderer();
    void testFilteredRuleBasedRenderer();
    void testAnimationExport();
    void testBillboardRendering();
    void testInstancedRendering();
    void testInstancedRenderingClipping();
    void testFilteredFlatTerrain();
    void testFilteredDemTerrain();
    void testFilteredExtrudedPolygons();
    void testDepthBuffer();
    void testAmbientOcclusion();
    void testDebugMap();
    void test3DSceneExporter();
    void test3DSceneExporterBig();

  private:
    QImage convertDepthImageToGrayscaleImage( const QImage &depthImage );

    void do3DSceneExport( const QString &testName, int zoomLevelsCount, int expectedObjectCount, int expectedFeatureCount, int maxFaceCount, Qgs3DMapScene *scene, QgsVectorLayer *layerPoly, QgsOffscreen3DEngine *engine, QgsTerrainEntity *terrainEntity = nullptr );

    std::unique_ptr<QgsProject> mProject;
    QgsRasterLayer *mLayerDtm = nullptr;
    QgsRasterLayer *mLayerRgb = nullptr;
    QgsVectorLayer *mLayerBuildings = nullptr;
};

QImage TestQgs3DRendering::convertDepthImageToGrayscaleImage( const QImage &depthImage )
{
  const int width = depthImage.width();
  const int height = depthImage.height();
  QImage grayImage( width, height, QImage::Format_ARGB32 );

  // compute image min/max depth values
  double minV = 9999999.0;
  double maxV = -9999999.0;
  for ( int y = 0; y < height; ++y )
  {
    const QRgb *depthImageScanline = reinterpret_cast<const QRgb *>( depthImage.scanLine( y ) );
    for ( int x = 0; x < width; ++x )
    {
      const double d = Qgs3DUtils::decodeDepth( depthImageScanline[x] );
      if ( d > maxV )
        maxV = d;
      if ( d < minV )
        minV = d;
    }
  }

  // transform depth value to gray value
  const double factor = ( maxV > minV ) ? 255.0 / ( maxV - minV ) : 1.0;
  for ( int y = 0; y < height; ++y )
  {
    const QRgb *depthImageScanline = reinterpret_cast<const QRgb *>( depthImage.scanLine( y ) );
    QRgb *grayImageScanline = reinterpret_cast<QRgb *>( grayImage.scanLine( y ) );
    for ( int x = 0; x < width; ++x )
    {
      const double d = Qgs3DUtils::decodeDepth( depthImageScanline[x] );
      unsigned short v = static_cast<unsigned short>( factor * ( d - minV ) );
      grayImageScanline[x] = qRgb( v, v, v );
    }
  }

  return grayImage;
}

// runs before all tests
void TestQgs3DRendering::initTestCase()
{
  // init QGIS's paths - true means that all path will be inited from prefix
  QgsApplication::init();
  QgsApplication::initQgis();
  Qgs3D::initialize();

  mProject.reset( new QgsProject );

  mLayerDtm = new QgsRasterLayer( testDataPath( "/3d/dtm.tif" ), "dtm", "gdal" );
  QVERIFY( mLayerDtm->isValid() );
  mProject->addMapLayer( mLayerDtm );

  mLayerRgb = new QgsRasterLayer( testDataPath( "/3d/rgb.tif" ), "rgb", "gdal" );
  QVERIFY( mLayerRgb->isValid() );
  mProject->addMapLayer( mLayerRgb );

  mLayerBuildings = new QgsVectorLayer( testDataPath( "/3d/buildings.shp" ), "buildings", "ogr" );
  QVERIFY( mLayerBuildings->isValid() );
  mProject->addMapLayer( mLayerBuildings );

  // best to keep buildings without 2D renderer so it is not painted on the terrain
  // so we do not get some possible artifacts
  mLayerBuildings->setRenderer( nullptr );

  QgsPhongMaterialSettings materialSettings;
  materialSettings.setAmbient( Qt::lightGray );
  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );
  QgsVectorLayer3DRenderer *renderer3d = new QgsVectorLayer3DRenderer( symbol3d );
  mLayerBuildings->setRenderer3D( renderer3d );

  mProject->setCrs( mLayerDtm->crs() );

  //
  // prepare styles for DTM layer
  //

  mLayerDtm->styleManager()->addStyleFromLayer( "grayscale" );

  double vMin = 44, vMax = 198;
  QColor cMin = Qt::red, cMax = Qt::yellow;

  // change renderer for the new style
  auto colorRampShader = std::make_unique<QgsColorRampShader>( vMin, vMax );
  colorRampShader->setColorRampItemList( QList<QgsColorRampShader::ColorRampItem>() << QgsColorRampShader::ColorRampItem( vMin, cMin ) << QgsColorRampShader::ColorRampItem( vMax, cMax ) );
  auto shader = std::make_unique<QgsRasterShader>( vMin, vMax );
  shader->setRasterShaderFunction( colorRampShader.release() );
  QgsSingleBandPseudoColorRenderer *r = new QgsSingleBandPseudoColorRenderer( mLayerDtm->renderer()->input(), 1, shader.release() );
  mLayerDtm->setRenderer( r );
  mLayerDtm->styleManager()->addStyleFromLayer( "my_style" );

  mLayerDtm->styleManager()->setCurrentStyle( "grayscale" );

  //
  // add map theme
  //

  QgsMapThemeCollection::MapThemeLayerRecord layerRecord( mLayerDtm );
  layerRecord.usingCurrentStyle = true;
  layerRecord.currentStyle = "my_style";
  QgsMapThemeCollection::MapThemeRecord record;
  record.addLayerRecord( layerRecord );
  mProject->mapThemeCollection()->insert( "theme_dtm", record );
}

//runs after all tests
void TestQgs3DRendering::cleanupTestCase()
{
  mProject.reset();
  QgsApplication::exitQgis();
}

void TestQgs3DRendering::testLights()
{
  // test light change signals
  Qgs3DMapSettings map;
  QSignalSpy lightSourceChangedSpy( &map, &Qgs3DMapSettings::lightSourcesChanged );

  QCOMPARE( map.lightSources().size(), 0 );
  map.setLightSources( {} );
  QCOMPARE( lightSourceChangedSpy.size(), 0 );

  QgsDirectionalLightSettings *defaultLight = new QgsDirectionalLightSettings();
  ;
  map.setLightSources( { defaultLight } );
  QCOMPARE( lightSourceChangedSpy.size(), 1 );
  // set identical light sources, should be no signal
  map.setLightSources( { defaultLight->clone() } );
  QCOMPARE( lightSourceChangedSpy.size(), 1 );

  // different light settings
  QgsDirectionalLightSettings *dsLight = new QgsDirectionalLightSettings();
  dsLight->setColor( QColor( 255, 0, 0 ) );
  map.setLightSources( { dsLight } );
  QCOMPARE( lightSourceChangedSpy.size(), 2 );
  // different light type
  auto pointLight = std::make_unique<QgsPointLightSettings>();
  pointLight->setColor( QColor( 255, 0, 0 ) );
  map.setLightSources( { pointLight->clone() } );
  QCOMPARE( lightSourceChangedSpy.size(), 3 );
  // different number of lights
  map.setLightSources( { pointLight->clone(), new QgsDirectionalLightSettings() } );
  QCOMPARE( lightSourceChangedSpy.size(), 4 );

  // a mix of types, but the same settings. Should be no new signals
  map.setLightSources( { pointLight->clone(), new QgsDirectionalLightSettings() } );
  QCOMPARE( lightSourceChangedSpy.size(), 4 );
}

void TestQgs3DRendering::testFlatTerrain()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerRgb );

  QgsFlatTerrainSettings *flatTerrainSettings = new QgsFlatTerrainSettings;
  map->setTerrainSettings( flatTerrainSettings );

  std::unique_ptr<QgsTerrainGenerator> generator = flatTerrainSettings->createTerrainGenerator( Qgs3DRenderContext::fromMapSettings( map ) );
  QVERIFY( dynamic_cast<QgsFlatTerrainGenerator *>( generator.get() )->isValid() );
  QCOMPARE( dynamic_cast<QgsFlatTerrainGenerator *>( generator.get() )->crs(), map->crs() );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  // look from the top
  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 0, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "flat_terrain_1", "flat_terrain_1", img, QString(), 40, QSize( 0, 0 ), 2 );

  // tilted view (pitch = 60 degrees)
  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 60, 0 );
  QImage img2 = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "flat_terrain_2", "flat_terrain_2", img2, QString(), 40, QSize( 0, 0 ), 2 );

  // also add horizontal rotation (yaw = 45 degrees)
  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 60, 45 );
  QImage img3 = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "flat_terrain_3", "flat_terrain_3", img3, QString(), 40, QSize( 0, 0 ), 2 );

  // change camera lens field of view
  map->setFieldOfView( 85.0f );
  QImage img4 = Qgs3DUtils::captureSceneImage( engine, scene );

  delete scene;
  delete map;

  QGSVERIFYIMAGECHECK( "flat_terrain_4", "flat_terrain_4", img4, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testDemTerrain()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerRgb );

  QgsDemTerrainSettings *demTerrainSettings = new QgsDemTerrainSettings;
  demTerrainSettings->setLayer( mLayerDtm );
  demTerrainSettings->setVerticalScale( 3 );
  map->setTerrainSettings( demTerrainSettings );

  std::unique_ptr<QgsTerrainGenerator> generator = demTerrainSettings->createTerrainGenerator( Qgs3DRenderContext::fromMapSettings( map ) );
  QVERIFY( dynamic_cast<QgsDemTerrainGenerator *>( generator.get() )->isValid() );
  QCOMPARE( dynamic_cast<QgsDemTerrainGenerator *>( generator.get() )->layer(), mLayerDtm );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2000, 60, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img3 = Qgs3DUtils::captureSceneImage( engine, scene );

  delete scene;
  delete map;

  QGSVERIFYIMAGECHECK( "dem_terrain_1", "dem_terrain_1", img3, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testTerrainShading()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  // no terrain layers set!

  QgsDemTerrainSettings *demTerrainSettings = new QgsDemTerrainSettings;
  demTerrainSettings->setLayer( mLayerDtm );
  demTerrainSettings->setVerticalScale( 3 );
  map->setTerrainSettings( demTerrainSettings );

  QgsPhongMaterialSettings terrainMaterial;
  terrainMaterial.setAmbient( QColor( 0, 0, 0 ) );
  terrainMaterial.setDiffuse( QColor( 255, 255, 0 ) );
  terrainMaterial.setSpecular( QColor( 255, 255, 255 ) );
  map->setTerrainShadingMaterial( terrainMaterial );
  map->setTerrainShadingEnabled( true );

  QgsPointLightSettings defaultLight;
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  defaultLight.setIntensity( 0.5 );
  map->setLightSources( { defaultLight.clone() } );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2000, 60, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img3 = Qgs3DUtils::captureSceneImage( engine, scene );
  delete scene;
  delete map;

  QGSVERIFYIMAGECHECK( "shaded_terrain_no_layers", "shaded_terrain_no_layers", img3, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testExtrudedPolygons()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerBuildings << mLayerRgb );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, -250, 0 ), 500, 45, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "polygon3d_extrusion", "polygon3d_extrusion", img, QString(), 40, QSize( 0, 0 ), 2 );

  // change opacity
  QgsPhongMaterialSettings materialSettings;
  materialSettings.setAmbient( Qt::lightGray );
  materialSettings.setOpacity( 0.5f );
  QgsPolygon3DSymbol *symbol3dOpacity = new QgsPolygon3DSymbol;
  symbol3dOpacity->setMaterialSettings( materialSettings.clone() );
  symbol3dOpacity->setExtrusionHeight( 10.f );
  QgsVectorLayer3DRenderer *renderer3dOpacity = new QgsVectorLayer3DRenderer( symbol3dOpacity );
  mLayerBuildings->setRenderer3D( renderer3dOpacity );
  QImage img2 = Qgs3DUtils::captureSceneImage( engine, scene );
  delete scene;
  delete map;

  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_opacity", "polygon3d_extrusion_opacity", img2, QString(), 60, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testExtrudedPolygonsClipping()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerBuildings << mLayerRgb );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsPhongMaterialSettings materialSettings;
  materialSettings.setAmbient( Qt::lightGray );
  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );
  QgsVectorLayer3DRenderer *renderer3d = new QgsVectorLayer3DRenderer( symbol3d );
  mLayerBuildings->setRenderer3D( renderer3d );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, -250, 0 ), 500, 45, 0 );

  // First, without clipping
  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img_no_clipping = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "polygon3d_extrusion", "polygon3d_extrusion", img_no_clipping, QString(), 40, QSize( 0, 0 ), 2 );

  // Enable clipping
  const QList<QVector4D> clipPlanesEquations = QList<QVector4D>()
                                               << QVector4D( 0.866025, -0.5, 0, 150.0 )
                                               << QVector4D( -0.866025, 0.5, 0, 150.0 )
                                               << QVector4D( 0.5, 0.866025, 0, 305.0 )
                                               << QVector4D( -0.5, -0.866025, 0, 205.0 );
  scene->enableClipping( clipPlanesEquations );

  QImage img_clipping = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_clipping", "polygon3d_extrusion_clipping", img_clipping, QString(), 40, QSize( 0, 0 ), 2 );

  // Disable clipping
  scene->disableClipping();
  QImage img_no_clipping_again = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "polygon3d_extrusion", "polygon3d_extrusion", img_no_clipping_again, QString(), 40, QSize( 0, 0 ), 2 );

  // Enable clipping a second time
  scene->enableClipping( clipPlanesEquations );

  QImage img_clipping_again = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_clipping", "polygon3d_extrusion_clipping", img_clipping_again, QString(), 40, QSize( 0, 0 ), 2 );

  // Disable clipping again
  scene->disableClipping();
  QImage img_no_clipping_final = Qgs3DUtils::captureSceneImage( engine, scene );

  delete map;
  delete scene;
  QGSVERIFYIMAGECHECK( "polygon3d_extrusion", "polygon3d_extrusion", img_no_clipping_final, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testPhongShading()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  auto buildings = std::make_unique<QgsVectorLayer>( testDataPath( "/3d/buildings.shp" ), "buildings", "ogr" );
  QVERIFY( buildings->isValid() );

  QgsPhongMaterialSettings materialSettings;
  materialSettings.setAmbient( QColor( 0, 100, 0 ) );
  materialSettings.setDiffuse( QColor( 255, 0, 255 ) );
  materialSettings.setSpecular( QColor( 0, 255, 255 ) );
  materialSettings.setShininess( 2 );
  materialSettings.setAmbientCoefficient( 0.3 );
  materialSettings.setDiffuseCoefficient( 0.8 );
  materialSettings.setSpecularCoefficient( 0.7 );
  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );
  QgsVectorLayer3DRenderer *renderer3d = new QgsVectorLayer3DRenderer( symbol3d );
  buildings->setRenderer3D( renderer3d );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << buildings.get() );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, -250, 0 ), 300, 45, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "phong_shading", "phong_shading", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testExtrudedPolygonsTexturedPhong()
{
  // In Qt 5, this test does not work on CI
#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )
  if ( QgsTest::isCIRun() )
  {
    QSKIP( "fails on CI" );
  }
#endif

  QgsPhongTexturedMaterialSettings materialSettings;
  materialSettings.setAmbient( QColor( 26, 26, 26 ) );
  materialSettings.setSpecular( QColor( 10, 10, 10 ) );
  materialSettings.setShininess( 1.0 );
  materialSettings.setDiffuseTexturePath( testDataPath( "/sample_image.png" ) );
  materialSettings.setTextureScale( 0.05 );
  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );
  QgsVectorLayer3DRenderer *renderer3d = new QgsVectorLayer3DRenderer( symbol3d );
  mLayerBuildings->setRenderer3D( renderer3d );

  const QgsRectangle fullExtent = mLayerDtm->extent();

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerBuildings << mLayerRgb );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 1.0 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( -60, -360, 10 ), 100, 45, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );

  delete scene;
  delete map;
  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_textured_phong", "polygon3d_extrusion_textured_phong", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testExtrudedPolygonsDataDefinedPhong()
{
  QgsPropertyCollection propertyColection;
  QgsProperty diffuseColor;
  QgsProperty ambientColor;
  QgsProperty specularColor;
  diffuseColor.setExpressionString( QStringLiteral( "color_rgb( 120*(\"ogc_fid\"%3),125,0)" ) );
  ambientColor.setExpressionString( QStringLiteral( "color_rgb( 120,(\"ogc_fid\"%2)*255,0)" ) );
  specularColor.setExpressionString( QStringLiteral( "'yellow'" ) );
  propertyColection.setProperty( QgsAbstractMaterialSettings::Property::Diffuse, diffuseColor );
  propertyColection.setProperty( QgsAbstractMaterialSettings::Property::Ambient, ambientColor );
  propertyColection.setProperty( QgsAbstractMaterialSettings::Property::Specular, specularColor );
  QgsPhongMaterialSettings materialSettings;
  materialSettings.setDataDefinedProperties( propertyColection );
  materialSettings.setAmbient( Qt::red );
  materialSettings.setShininess( 1 );
  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );
  QgsVectorLayer3DRenderer *renderer3d = new QgsVectorLayer3DRenderer( symbol3d );
  mLayerBuildings->setRenderer3D( renderer3d );

  const QgsRectangle fullExtent = mLayerDtm->extent();

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerBuildings << mLayerRgb );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, -250, 0 ), 500, 45, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );

  delete scene;
  delete map;
  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_data_defined_phong", "polygon3d_extrusion_data_defined_phong", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testExtrudedPolygonsDataDefinedPhongClipping()
{
  QgsPropertyCollection propertyColection;
  QgsProperty diffuseColor;
  QgsProperty ambientColor;
  QgsProperty specularColor;
  diffuseColor.setExpressionString( QStringLiteral( "color_rgb( 120*(\"ogc_fid\"%3),125,0)" ) );
  ambientColor.setExpressionString( QStringLiteral( "color_rgb( 120,(\"ogc_fid\"%2)*255,0)" ) );
  specularColor.setExpressionString( QStringLiteral( "'yellow'" ) );
  propertyColection.setProperty( QgsAbstractMaterialSettings::Property::Diffuse, diffuseColor );
  propertyColection.setProperty( QgsAbstractMaterialSettings::Property::Ambient, ambientColor );
  propertyColection.setProperty( QgsAbstractMaterialSettings::Property::Specular, specularColor );
  QgsPhongMaterialSettings materialSettings;
  materialSettings.setDataDefinedProperties( propertyColection );
  materialSettings.setAmbient( Qt::red );
  materialSettings.setShininess( 1 );
  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );
  QgsVectorLayer3DRenderer *renderer3d = new QgsVectorLayer3DRenderer( symbol3d );
  mLayerBuildings->setRenderer3D( renderer3d );

  const QgsRectangle fullExtent = mLayerDtm->extent();

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerBuildings << mLayerRgb );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, -250, 0 ), 500, 45, 0 );

  // First, without clipping
  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img_no_clipping = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_data_defined_phong", "polygon3d_extrusion_data_defined_phong", img_no_clipping, QString(), 40, QSize( 0, 0 ), 2 );

  // Enable clipping
  const QList<QVector4D> clipPlanesEquations = QList<QVector4D>()
                                               << QVector4D( 0.866025, -0.5, 0, 150.0 )
                                               << QVector4D( -0.866025, 0.5, 0, 150.0 )
                                               << QVector4D( 0.5, 0.866025, 0, 305.0 )
                                               << QVector4D( -0.5, -0.866025, 0, 205.0 );
  scene->enableClipping( clipPlanesEquations );
  QImage img_clipping = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_data_defined_phong_clipping", "polygon3d_extrusion_data_defined_phong_clipping", img_clipping, QString(), 40, QSize( 0, 0 ), 2 );

  // Disable clipping
  scene->disableClipping();

  QImage img_no_clipping_again = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_data_defined_phong", "polygon3d_extrusion_data_defined_phong", img_no_clipping_again, QString(), 40, QSize( 0, 0 ), 2 );

  // Enable clipping a second time
  scene->enableClipping( clipPlanesEquations );

  QImage img_clipping_again = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_data_defined_phong_clipping", "polygon3d_extrusion_data_defined_phong_clipping", img_clipping_again, QString(), 40, QSize( 0, 0 ), 2 );

  // Disable clipping again
  scene->disableClipping();

  QImage img_no_clipping_final = Qgs3DUtils::captureSceneImage( engine, scene );
  delete scene;
  delete map;
  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_data_defined_phong", "polygon3d_extrusion_data_defined_phong", img_no_clipping_final, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testExtrudedPolygonsDataDefinedGooch()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  QgsPropertyCollection propertyColection;
  QgsProperty diffuseColor;
  QgsProperty warmColor;
  QgsProperty coolColor;
  diffuseColor.setExpressionString( QStringLiteral( "color_rgb( 120*(\"ogc_fid\"%3),125,0)" ) );
  warmColor.setExpressionString( QStringLiteral( "color_rgb( 120,(\"ogc_fid\"%2)*255,0)" ) );
  coolColor.setExpressionString( QStringLiteral( "'yellow'" ) );
  propertyColection.setProperty( QgsAbstractMaterialSettings::Property::Diffuse, diffuseColor );
  propertyColection.setProperty( QgsAbstractMaterialSettings::Property::Warm, warmColor );
  propertyColection.setProperty( QgsAbstractMaterialSettings::Property::Cool, coolColor );
  QgsGoochMaterialSettings materialSettings;
  materialSettings.setDataDefinedProperties( propertyColection );
  materialSettings.setAlpha( 0.2f );
  materialSettings.setBeta( 0.6f );
  materialSettings.setShininess( 2 );

  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );
  QgsVectorLayer3DRenderer *renderer3d = new QgsVectorLayer3DRenderer( symbol3d );
  mLayerBuildings->setRenderer3D( renderer3d );

  Qgs3DMapSettings *mapSettings = new Qgs3DMapSettings;
  mapSettings->setCrs( mProject->crs() );
  mapSettings->setExtent( fullExtent );
  mapSettings->setLayers( QList<QgsMapLayer *>() << mLayerBuildings << mLayerRgb );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  mapSettings->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( mapSettings->crs(), mProject->transformContext() );
  mapSettings->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *mapSettings, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, -250, 0 ), 500, 45, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );

  delete scene;
  delete mapSettings;
  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_data_defined_gooch", "polygon3d_extrusion_data_defined_gooch", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testExtrudedPolygonsDataDefinedGoochClipping()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  QgsPropertyCollection propertyColection;
  QgsProperty diffuseColor;
  QgsProperty warmColor;
  QgsProperty coolColor;
  diffuseColor.setExpressionString( QStringLiteral( "color_rgb( 120*(\"ogc_fid\"%3),125,0)" ) );
  warmColor.setExpressionString( QStringLiteral( "color_rgb( 120,(\"ogc_fid\"%2)*255,0)" ) );
  coolColor.setExpressionString( QStringLiteral( "'yellow'" ) );
  propertyColection.setProperty( QgsAbstractMaterialSettings::Property::Diffuse, diffuseColor );
  propertyColection.setProperty( QgsAbstractMaterialSettings::Property::Warm, warmColor );
  propertyColection.setProperty( QgsAbstractMaterialSettings::Property::Cool, coolColor );
  QgsGoochMaterialSettings materialSettings;
  materialSettings.setDataDefinedProperties( propertyColection );
  materialSettings.setAlpha( 0.2f );
  materialSettings.setBeta( 0.6f );
  materialSettings.setShininess( 2 );

  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );
  QgsVectorLayer3DRenderer *renderer3d = new QgsVectorLayer3DRenderer( symbol3d );
  mLayerBuildings->setRenderer3D( renderer3d );

  Qgs3DMapSettings *mapSettings = new Qgs3DMapSettings;
  mapSettings->setCrs( mProject->crs() );
  mapSettings->setExtent( fullExtent );
  mapSettings->setLayers( QList<QgsMapLayer *>() << mLayerBuildings << mLayerRgb );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  mapSettings->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( mapSettings->crs(), mProject->transformContext() );
  mapSettings->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *mapSettings, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, -250, 0 ), 500, 45, 0 );

  // First, without clipping
  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img_no_clipping = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_data_defined_gooch", "polygon3d_extrusion_data_defined_gooch", img_no_clipping, QString(), 40, QSize( 0, 0 ), 2 );

  // Enable clipping
  const QList<QVector4D> clipPlanesEquations = QList<QVector4D>()
                                               << QVector4D( 0.866025, -0.5, 0, 150.0 )
                                               << QVector4D( -0.866025, 0.5, 0, 150.0 )
                                               << QVector4D( 0.5, 0.866025, 0, 305.0 )
                                               << QVector4D( -0.5, -0.866025, 0, 205.0 );
  scene->enableClipping( clipPlanesEquations );

  QImage img_clipping = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_data_defined_gooch_clipping", "polygon3d_extrusion_data_defined_gooch_clipping", img_clipping, QString(), 40, QSize( 0, 0 ), 2 );

  // Disable clipping
  scene->disableClipping();
  QImage img_no_clipping_again = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_data_defined_gooch", "polygon3d_extrusion_data_defined_gooch", img_no_clipping_again, QString(), 40, QSize( 0, 0 ), 2 );

  // Enable clipping a second time
  scene->enableClipping( clipPlanesEquations );

  QImage img_clipping_again = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_data_defined_gooch_clipping", "polygon3d_extrusion_data_defined_gooch_clipping", img_clipping_again, QString(), 40, QSize( 0, 0 ), 2 );

  // Disable clipping again
  scene->disableClipping();
  QImage img_no_clipping_final = Qgs3DUtils::captureSceneImage( engine, scene );

  delete scene;
  delete mapSettings;
  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_data_defined_gooch", "polygon3d_extrusion_data_defined_gooch", img_no_clipping_final, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testExtrudedPolygonsGoochShading()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerBuildings << mLayerRgb );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  QgsGoochMaterialSettings materialSettings;
  materialSettings.setWarm( QColor( 224, 224, 17 ) );
  materialSettings.setCool( QColor( 21, 187, 235 ) );
  materialSettings.setAlpha( 0.2f );
  materialSettings.setBeta( 0.6f );
  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );
  QgsVectorLayer3DRenderer *renderer3dOpacity = new QgsVectorLayer3DRenderer( symbol3d );
  mLayerBuildings->setRenderer3D( renderer3dOpacity );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, -250, 0 ), 500, 45, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );

  delete scene;
  delete map;

  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_gooch_shading", "polygon3d_extrusion_gooch_shading", img, QString(), 50, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testExtrudedPolygonsMetalRoughShading()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  auto buildings = std::make_unique<QgsVectorLayer>( testDataPath( "/3d/buildings.shp" ), "buildings", "ogr" );
  QVERIFY( buildings->isValid() );

  QgsMetalRoughMaterialSettings materialSettings;
  materialSettings.setBaseColor( QColor( 255, 0, 255 ) );
  materialSettings.setMetalness( 0.5 );
  materialSettings.setRoughness( 0.3 );

  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );
  QgsVectorLayer3DRenderer *renderer3d = new QgsVectorLayer3DRenderer( symbol3d );
  buildings->setRenderer3D( renderer3d );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << buildings.get() );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.9 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), mProject->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, -250, 0 ), 300, 45, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "metal_rough", "metal_rough", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testPolygonsEdges()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );

  QgsPhongMaterialSettings materialSettings;
  materialSettings.setAmbient( Qt::lightGray );
  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );
  symbol3d->setOffset( 20.f );
  symbol3d->setEdgesEnabled( true );
  symbol3d->setEdgeWidth( 8 );
  symbol3d->setEdgeColor( QColor( 255, 0, 0 ) );

  std::unique_ptr<QgsVectorLayer> layer( mLayerBuildings->clone() );

  auto simpleFill = std::make_unique<QgsSimpleFillSymbolLayer>( QColor( 0, 0, 0 ), Qt::SolidPattern, QColor( 0, 0, 0 ), Qt::NoPen );
  auto fillSymbol = std::make_unique<QgsFillSymbol>( QgsSymbolLayerList() << simpleFill.release() );
  layer->setRenderer( new QgsSingleSymbolRenderer( fillSymbol.release() ) );

  QgsVectorLayer3DRenderer *renderer3d = new QgsVectorLayer3DRenderer( symbol3d );
  layer->setRenderer3D( renderer3d );

  map->setLayers( QList<QgsMapLayer *>() << layer.get() );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, -250, 0 ), 100, 45, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );

  delete scene;
  delete map;

  QGSVERIFYIMAGECHECK( "polygon_edges_height", "polygon_edges_height", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testLineRendering()
{
  const QgsRectangle fullExtent( 0, 0, 1000, 1000 );

  QgsVectorLayer *layerLines = new QgsVectorLayer( "LineString?crs=EPSG:27700", "lines", "memory" );

  QgsLine3DSymbol *lineSymbol = new QgsLine3DSymbol;
  lineSymbol->setRenderAsSimpleLines( true );
  lineSymbol->setWidth( 10 );
  QgsSimpleLineMaterialSettings matSettings;
  matSettings.setAmbient( Qt::red );
  lineSymbol->setMaterialSettings( matSettings.clone() );
  layerLines->setRenderer3D( new QgsVectorLayer3DRenderer( lineSymbol ) );

  QVector<QgsPoint> pts;
  pts << QgsPoint( 0, 0, 10 ) << QgsPoint( 0, 1000, 10 ) << QgsPoint( 1000, 1000, 10 ) << QgsPoint( 1000, 0, 10 );
  pts << QgsPoint( 1000, 0, 500 ) << QgsPoint( 1000, 1000, 500 ) << QgsPoint( 0, 1000, 500 ) << QgsPoint( 0, 0, 500 );
  QgsFeature f1( layerLines->fields() );
  f1.setGeometry( QgsGeometry( new QgsLineString( pts ) ) );
  QgsFeatureList flist;
  flist << f1;
  layerLines->dataProvider()->addFeatures( flist );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << layerLines );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  // look from the top
  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 0, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "line_rendering_1", "line_rendering_1", img, QString(), 40, QSize( 0, 0 ), 2 );

  // more perspective look
  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 45, 45 );

  QImage img2 = Qgs3DUtils::captureSceneImage( engine, scene );
  delete scene;
  delete map;
  delete layerLines;
  QGSVERIFYIMAGECHECK( "line_rendering_2", "line_rendering_2", img2, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testLineRenderingClipping()
{
  const QgsRectangle fullExtent( 0, 0, 1000, 1000 );

  QgsVectorLayer *layerLines = new QgsVectorLayer( "LineString?crs=EPSG:27700", "lines", "memory" );

  QgsLine3DSymbol *lineSymbol = new QgsLine3DSymbol;
  lineSymbol->setRenderAsSimpleLines( true );
  lineSymbol->setWidth( 10 );
  QgsSimpleLineMaterialSettings matSettings;
  matSettings.setAmbient( Qt::red );
  lineSymbol->setMaterialSettings( matSettings.clone() );
  layerLines->setRenderer3D( new QgsVectorLayer3DRenderer( lineSymbol ) );

  QVector<QgsPoint> pts;
  pts << QgsPoint( 0, 0, 10 ) << QgsPoint( 0, 1000, 10 ) << QgsPoint( 1000, 1000, 10 ) << QgsPoint( 1000, 0, 10 );
  pts << QgsPoint( 1000, 0, 500 ) << QgsPoint( 1000, 1000, 500 ) << QgsPoint( 0, 1000, 500 ) << QgsPoint( 0, 0, 500 );
  QgsFeature f1( layerLines->fields() );
  f1.setGeometry( QgsGeometry( new QgsLineString( pts ) ) );
  QgsFeatureList flist;
  flist << f1;
  layerLines->dataProvider()->addFeatures( flist );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << layerLines );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  // look from the top
  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 0, 0 );

  // First, without clipping
  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img_no_clipping = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "line_rendering_1", "line_rendering_1", img_no_clipping, QString(), 40, QSize( 0, 0 ), 2 );

  // Enable clipping
  const QList<QVector4D> clipPlanesEquations = QList<QVector4D>()
                                               << QVector4D( 0.866025, -0.5, 0, 300.0 )
                                               << QVector4D( -0.866025, 0.5, 0, 300.0 );
  scene->enableClipping( clipPlanesEquations );

  QImage img_clipping = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "line_rendering_clipping", "line_rendering_clipping", img_clipping, QString(), 40, QSize( 0, 0 ), 2 );

  // Disable clipping
  scene->disableClipping();
  QImage img_no_clipping_again = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "line_rendering_1", "line_rendering_1", img_no_clipping_again, QString(), 40, QSize( 0, 0 ), 2 );

  // Enable clipping a second time
  scene->enableClipping( clipPlanesEquations );

  QImage img_clipping_again = Qgs3DUtils::captureSceneImage( engine, scene );

  QGSVERIFYIMAGECHECK( "line_rendering_clipping", "line_rendering_clipping", img_clipping_again, QString(), 40, QSize( 0, 0 ), 2 );

  // Disable clipping again
  scene->disableClipping();

  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img_no_clipping_final = Qgs3DUtils::captureSceneImage( engine, scene );
  delete scene;
  delete map;
  QGSVERIFYIMAGECHECK( "line_rendering_1", "line_rendering_1", img_no_clipping_final, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testLineRenderingCurved()
{
  // test rendering of compound curve lines works
  const QgsRectangle fullExtent( 0, 0, 1000, 1000 );

  QgsVectorLayer *layerLines = new QgsVectorLayer( "CompoundCurve?crs=EPSG:27700", "lines", "memory" );

  QgsLine3DSymbol *lineSymbol = new QgsLine3DSymbol;
  lineSymbol->setRenderAsSimpleLines( true );
  lineSymbol->setWidth( 10 );
  QgsSimpleLineMaterialSettings matSettings;
  matSettings.setAmbient( Qt::red );
  lineSymbol->setMaterialSettings( matSettings.clone() );
  layerLines->setRenderer3D( new QgsVectorLayer3DRenderer( lineSymbol ) );

  QVector<QgsPoint> pts;
  pts << QgsPoint( 0, 0, 10 ) << QgsPoint( 0, 1000, 10 ) << QgsPoint( 1000, 1000, 10 ) << QgsPoint( 1000, 0, 10 );
  auto curve = std::make_unique<QgsCompoundCurve>();
  curve->addCurve( new QgsLineString( pts ) );
  pts.clear();
  pts << QgsPoint( 1000, 0, 10 ) << QgsPoint( 1000, 0, 500 ) << QgsPoint( 1000, 1000, 500 ) << QgsPoint( 0, 1000, 500 ) << QgsPoint( 0, 0, 500 );
  curve->addCurve( new QgsLineString( pts ) );

  QgsFeature f1( layerLines->fields() );
  f1.setGeometry( QgsGeometry( std::move( curve ) ) );
  QgsFeatureList flist;
  flist << f1;
  layerLines->dataProvider()->addFeatures( flist );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << layerLines );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  // look from the top
  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 0, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );
  delete scene;
  delete map;
  delete layerLines;
  QGSVERIFYIMAGECHECK( "line_rendering_1", "line_rendering_1", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testLineRenderingDataDefinedColors()
{
  const QgsRectangle fullExtent( 0, 0, 1000, 1000 );

  QgsVectorLayer *layerLines = new QgsVectorLayer( "LineString?crs=EPSG:27700&field=category:string(20)", "lines", "memory" );

  QgsLine3DSymbol *lineSymbol = new QgsLine3DSymbol;
  lineSymbol->setRenderAsSimpleLines( true );
  lineSymbol->setWidth( 10 );
  QgsSimpleLineMaterialSettings matSettings;
  matSettings.setAmbient( Qt::red );
  QgsPropertyCollection properties;
  properties.setProperty( QgsSimpleLineMaterialSettings::Property::Ambient, QgsProperty::fromExpression( QStringLiteral( "case when \"category\" = 'blue' then '#2233cc' when \"category\" = 'green' then '#33ff55' end" ) ) );
  matSettings.setDataDefinedProperties( properties );
  lineSymbol->setMaterialSettings( matSettings.clone() );
  layerLines->setRenderer3D( new QgsVectorLayer3DRenderer( lineSymbol ) );

  QVector<QgsPoint> pts;
  pts << QgsPoint( 0, 0, 10 ) << QgsPoint( 0, 1000, 10 ) << QgsPoint( 1000, 1000, 10 ) << QgsPoint( 1000, 0, 10 );
  QgsFeature f1( layerLines->fields() );
  f1.setGeometry( QgsGeometry( new QgsLineString( pts ) ) );
  f1.setAttributes( QgsAttributes( { QStringLiteral( "blue" ) } ) );
  pts.clear();
  pts << QgsPoint( 1000, 0, 500 ) << QgsPoint( 1000, 1000, 500 ) << QgsPoint( 0, 1000, 500 ) << QgsPoint( 0, 0, 500 );
  QgsFeature f2( layerLines->fields() );
  f2.setGeometry( QgsGeometry( new QgsLineString( pts ) ) );
  f2.setAttributes( QgsAttributes( { QStringLiteral( "green" ) } ) );
  QgsFeatureList flist;
  flist << f1 << f2;
  layerLines->dataProvider()->addFeatures( flist );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << layerLines );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  // look from the top
  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 0, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "line_rendering_data_defined_color", "line_rendering_data_defined_color", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testBufferedLineRendering()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  QgsVectorLayer *layerLines = new QgsVectorLayer( testDataPath( "/3d/lines.shp" ), "lines", "ogr" );
  QVERIFY( layerLines->isValid() );

  QgsLine3DSymbol *lineSymbol = new QgsLine3DSymbol;
  lineSymbol->setWidth( 10 );
  lineSymbol->setExtrusionHeight( 30 );
  QgsPhongMaterialSettings matSettings;
  matSettings.setAmbient( Qt::red );
  lineSymbol->setMaterialSettings( matSettings.clone() );
  layerLines->setRenderer3D( new QgsVectorLayer3DRenderer( lineSymbol ) );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << layerLines );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 300, -250, 0 ), 500, 45, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );
  delete scene;
  delete map;
  delete layerLines;

  QGSVERIFYIMAGECHECK( "buffered_lines", "buffered_lines", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testBufferedLineRenderingClipping()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  QgsVectorLayer *layerLines = new QgsVectorLayer( testDataPath( "/3d/lines.shp" ), "lines", "ogr" );
  QVERIFY( layerLines->isValid() );

  QgsLine3DSymbol *lineSymbol = new QgsLine3DSymbol;
  lineSymbol->setWidth( 10 );
  lineSymbol->setExtrusionHeight( 30 );
  QgsPhongMaterialSettings matSettings;
  matSettings.setAmbient( Qt::red );
  lineSymbol->setMaterialSettings( matSettings.clone() );
  layerLines->setRenderer3D( new QgsVectorLayer3DRenderer( lineSymbol ) );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << layerLines );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 300, -250, 0 ), 500, 45, 0 );

  // First, without clipping
  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img_no_clipping = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "buffered_lines", "buffered_lines", img_no_clipping, QString(), 40, QSize( 0, 0 ), 2 );

  // Enable clipping
  const QList<QVector4D> clipPlanesEquations = QList<QVector4D>()
                                               << QVector4D( -0.866025, 0.5, 0, 432.0 )
                                               << QVector4D( 0.5, 0.866025, 0, 125.0 );

  scene->enableClipping( clipPlanesEquations );

  QImage img_clipping = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "buffered_lines_clipping", "buffered_lines_clipping", img_clipping, QString(), 40, QSize( 0, 0 ), 2 );

  // Disable clipping
  scene->disableClipping();

  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img_no_clipping_again = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "buffered_lines", "buffered_lines", img_no_clipping_again, QString(), 40, QSize( 0, 0 ), 2 );

  // Enable clipping a second time
  scene->enableClipping( clipPlanesEquations );

  QImage img_clipping_again = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "buffered_lines_clipping", "buffered_lines_clipping", img_clipping_again, QString(), 40, QSize( 0, 0 ), 2 );

  // Disable clipping again
  scene->disableClipping();

  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img_no_clipping_final = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "buffered_lines", "buffered_lines", img_no_clipping_final, QString(), 40, QSize( 0, 0 ), 2 );

  delete scene;
  delete map;
  delete layerLines;
}

void TestQgs3DRendering::testBufferedLineRenderingWidth()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  QgsVectorLayer *layerLines = new QgsVectorLayer( testDataPath( "/3d/lines.shp" ), "lines", "ogr" );
  QVERIFY( layerLines->isValid() );

  QgsLine3DSymbol *lineSymbol = new QgsLine3DSymbol;
  lineSymbol->setWidth( 20 );
  lineSymbol->setExtrusionHeight( 30 );
  lineSymbol->setOffset( 10 );
  QgsPhongMaterialSettings matSettings;
  matSettings.setAmbient( Qt::red );
  lineSymbol->setMaterialSettings( matSettings.clone() );
  layerLines->setRenderer3D( new QgsVectorLayer3DRenderer( lineSymbol ) );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << layerLines );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 300, -250, 0 ), 500, 45, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );
  delete scene;
  delete map;
  delete layerLines;
  QGSVERIFYIMAGECHECK( "buffered_lines_width", "buffered_lines_width", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testMapTheme()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerDtm );

  // set theme - this should override what we set in setLayers()
  map->setMapThemeCollection( mProject->mapThemeCollection() );
  map->setTerrainMapTheme( "theme_dtm" );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  // look from the top
  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 0, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );
  delete scene;
  delete map;
  QGSVERIFYIMAGECHECK( "terrain_theme", "terrain_theme", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testRuleBasedRenderer()
{
  QgsPhongMaterialSettings materialSettings;
  materialSettings.setAmbient( Qt::lightGray );
  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );

  QgsPhongMaterialSettings materialSettings2;
  materialSettings2.setAmbient( Qt::red );
  QgsPolygon3DSymbol *symbol3d2 = new QgsPolygon3DSymbol;
  symbol3d2->setMaterialSettings( materialSettings2.clone() );
  symbol3d2->setExtrusionHeight( 10.f );

  QgsRuleBased3DRenderer::Rule *root = new QgsRuleBased3DRenderer::Rule( nullptr );
  QgsRuleBased3DRenderer::Rule *rule1 = new QgsRuleBased3DRenderer::Rule( symbol3d, "ogc_fid < 29069", "rule 1" );
  QgsRuleBased3DRenderer::Rule *rule2 = new QgsRuleBased3DRenderer::Rule( symbol3d2, "ogc_fid >= 29069", "rule 2" );
  root->appendChild( rule1 );
  root->appendChild( rule2 );
  QgsRuleBased3DRenderer *renderer3d = new QgsRuleBased3DRenderer( root );
  mLayerBuildings->setRenderer3D( renderer3d );

  const QgsRectangle fullExtent = mLayerDtm->extent();

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerBuildings );

  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, -250, 0 ), 500, 45, 0 );

  // When running the test, it would sometimes return partially rendered image.
  // It is probably based on how fast qt3d manages to upload the data to GPU...
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );

  delete scene;
  delete map;

  QGSVERIFYIMAGECHECK( "rulebased", "rulebased", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testFilteredRuleBasedRenderer()
{
  QgsPhongMaterialSettings materialSettings;
  materialSettings.setAmbient( Qt::lightGray );
  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );

  QgsPhongMaterialSettings materialSettings2;
  materialSettings2.setAmbient( Qt::red );
  QgsPolygon3DSymbol *symbol3d2 = new QgsPolygon3DSymbol;
  symbol3d2->setMaterialSettings( materialSettings2.clone() );
  symbol3d2->setExtrusionHeight( 10.f );

  QgsRuleBased3DRenderer::Rule *root = new QgsRuleBased3DRenderer::Rule( nullptr );
  QgsRuleBased3DRenderer::Rule *rule1 = new QgsRuleBased3DRenderer::Rule( symbol3d, "ogc_fid < 29069", "rule 1" );
  QgsRuleBased3DRenderer::Rule *rule2 = new QgsRuleBased3DRenderer::Rule( symbol3d2, "ogc_fid >= 29069", "rule 2" );
  root->appendChild( rule1 );
  root->appendChild( rule2 );
  QgsRuleBased3DRenderer *renderer3d = new QgsRuleBased3DRenderer( root );
  mLayerBuildings->setRenderer3D( renderer3d );

  QgsRectangle fullExtent = mLayerBuildings->extent();
  const QgsRectangle filteredExtent = QgsRectangle( fullExtent.xMinimum(), fullExtent.yMinimum(), fullExtent.xMinimum() + 0.8 * fullExtent.width(), fullExtent.yMinimum() + 0.7 * fullExtent.height() );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( filteredExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerBuildings );

  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, -250, 0 ), 500, 45, 0 );

  // When running the test, it would sometimes return partially rendered image.
  // It is probably based on how fast qt3d manages to upload the data to GPU...
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );

  delete scene;
  delete map;

  QGSVERIFYIMAGECHECK( "rulebased_filtered", "rulebased_filtered", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testAnimationExport()
{
  const QgsRectangle fullExtent = mLayerDtm->extent();

  Qgs3DMapSettings map;
  map.setCrs( mProject->crs() );
  map.setExtent( fullExtent );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map.crs(), mProject->transformContext() );
  map.setTerrainGenerator( flatTerrain );

  Qgs3DAnimationSettings animSettings;
  Qgs3DAnimationSettings::Keyframes keyframes;
  Qgs3DAnimationSettings::Keyframe kf1;
  kf1.dist = 2500;
  Qgs3DAnimationSettings::Keyframe kf2;
  kf2.time = 2;
  kf2.dist = 3000;
  keyframes << kf1;
  keyframes << kf2;
  animSettings.setKeyframes( keyframes );

  const QString dir = QDir::temp().path();
  QString error;

  const bool success = Qgs3DUtils::exportAnimation(
    animSettings,
    map,
    1,
    dir,
    "test3danimation###.png",
    QSize( 600, 400 ),
    error,
    nullptr
  );

  QVERIFY( success );
  QVERIFY( QFileInfo::exists( ( QDir( dir ).filePath( QStringLiteral( "test3danimation001.png" ) ) ) ) );
}

void TestQgs3DRendering::testInstancedRendering()
{
  const QgsRectangle fullExtent( 1000, 1000, 2000, 2000 );

  auto layerPointsZ = std::make_unique<QgsVectorLayer>( "PointZ?crs=EPSG:27700", "points Z", "memory" );

  QgsPoint *p1 = new QgsPoint( 1000, 1000, 50 );
  QgsPoint *p2 = new QgsPoint( 1000, 2000, 100 );
  QgsPoint *p3 = new QgsPoint( 2000, 2000, 200 );

  QgsFeature f1( layerPointsZ->fields() );
  QgsFeature f2( layerPointsZ->fields() );
  QgsFeature f3( layerPointsZ->fields() );

  f1.setGeometry( QgsGeometry( p1 ) );
  f2.setGeometry( QgsGeometry( p2 ) );
  f3.setGeometry( QgsGeometry( p3 ) );

  QgsFeatureList featureList;
  featureList << f1 << f2 << f3;
  layerPointsZ->dataProvider()->addFeatures( featureList );

  QgsPoint3DSymbol *sphere3DSymbol = new QgsPoint3DSymbol();
  sphere3DSymbol->setShape( Qgis::Point3DShape::Sphere );
  QVariantMap vmSphere;
  vmSphere[QStringLiteral( "radius" )] = 80.0f;
  sphere3DSymbol->setShapeProperties( vmSphere );
  QgsPhongMaterialSettings materialSettings;
  materialSettings.setAmbient( Qt::gray );
  sphere3DSymbol->setMaterialSettings( materialSettings.clone() );

  layerPointsZ->setRenderer3D( new QgsVectorLayer3DRenderer( sphere3DSymbol ) );

  Qgs3DMapSettings *mapSettings = new Qgs3DMapSettings;
  mapSettings->setCrs( mProject->crs() );
  mapSettings->setExtent( fullExtent );
  mapSettings->setLayers( QList<QgsMapLayer *>() << layerPointsZ.get() );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( mapSettings->crs(), mapSettings->transformContext() );
  mapSettings->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *mapSettings, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 45, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage imgSphere = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "sphere_rendering", "sphere_rendering", imgSphere, QString(), 40, QSize( 0, 0 ), 2 );

  QgsPoint3DSymbol *cylinder3DSymbol = new QgsPoint3DSymbol();
  cylinder3DSymbol->setShape( Qgis::Point3DShape::Cylinder );
  QVariantMap vmCylinder;
  vmCylinder[QStringLiteral( "radius" )] = 20.0f;
  vmCylinder[QStringLiteral( "length" )] = 200.0f;
  cylinder3DSymbol->setShapeProperties( vmCylinder );
  cylinder3DSymbol->setMaterialSettings( materialSettings.clone() );

  layerPointsZ->setRenderer3D( new QgsVectorLayer3DRenderer( cylinder3DSymbol ) );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 60, 0 );

  QImage imgCylinder = Qgs3DUtils::captureSceneImage( engine, scene );
  delete scene;
  delete mapSettings;
  QGSVERIFYIMAGECHECK( "cylinder_rendering", "cylinder_rendering", imgCylinder, QString(), 40, QSize( 0, 0 ), 2 );
}


void TestQgs3DRendering::testInstancedRenderingClipping()
{
  const QgsRectangle fullExtent( 1000, 1000, 2000, 2000 );

  auto layerPointsZ = std::make_unique<QgsVectorLayer>( "PointZ?crs=EPSG:27700", "points Z", "memory" );

  QgsPoint *p1 = new QgsPoint( 1000, 1000, 50 );
  QgsPoint *p2 = new QgsPoint( 1000, 2000, 100 );
  QgsPoint *p3 = new QgsPoint( 2000, 2000, 200 );

  QgsFeature f1( layerPointsZ->fields() );
  QgsFeature f2( layerPointsZ->fields() );
  QgsFeature f3( layerPointsZ->fields() );

  f1.setGeometry( QgsGeometry( p1 ) );
  f2.setGeometry( QgsGeometry( p2 ) );
  f3.setGeometry( QgsGeometry( p3 ) );

  QgsFeatureList featureList;
  featureList << f1 << f2 << f3;
  layerPointsZ->dataProvider()->addFeatures( featureList );

  QgsPoint3DSymbol *sphere3DSymbol = new QgsPoint3DSymbol();
  sphere3DSymbol->setShape( Qgis::Point3DShape::Sphere );
  QVariantMap vmSphere;
  vmSphere[QStringLiteral( "radius" )] = 80.0f;
  sphere3DSymbol->setShapeProperties( vmSphere );
  QgsPhongMaterialSettings materialSettings;
  materialSettings.setAmbient( Qt::gray );
  sphere3DSymbol->setMaterialSettings( materialSettings.clone() );

  layerPointsZ->setRenderer3D( new QgsVectorLayer3DRenderer( sphere3DSymbol ) );

  Qgs3DMapSettings *mapSettings = new Qgs3DMapSettings;
  mapSettings->setCrs( mProject->crs() );
  mapSettings->setExtent( fullExtent );
  mapSettings->setLayers( QList<QgsMapLayer *>() << layerPointsZ.get() );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( mapSettings->crs(), mapSettings->transformContext() );
  mapSettings->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *mapSettings, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 45, 0 );

  QList<QVector4D> clipPlanesEquationsSphere = QList<QVector4D>()
                                               << QVector4D( 0.866025, -0.5, 0, 660.0 )
                                               << QVector4D( 0.5, 0.866025, 0, 685.0 )
                                               << QVector4D( -0.5, -0.866025, 0, 650.0 );
  scene->enableClipping( clipPlanesEquationsSphere );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage imgSphere = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "sphere_rendering_clipping", "sphere_rendering_clipping", imgSphere, QString(), 40, QSize( 0, 0 ), 2 );

  scene->disableClipping();

  Qgs3DUtils::captureSceneImage( engine, scene );
  imgSphere = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "sphere_rendering", "sphere_rendering", imgSphere, QString(), 40, QSize( 0, 0 ), 2 );

  QgsPoint3DSymbol *cylinder3DSymbol = new QgsPoint3DSymbol();
  cylinder3DSymbol->setShape( Qgis::Point3DShape::Cylinder );
  QVariantMap vmCylinder;
  vmCylinder[QStringLiteral( "radius" )] = 20.0f;
  vmCylinder[QStringLiteral( "length" )] = 200.0f;
  cylinder3DSymbol->setShapeProperties( vmCylinder );
  cylinder3DSymbol->setMaterialSettings( materialSettings.clone() );

  layerPointsZ->setRenderer3D( new QgsVectorLayer3DRenderer( cylinder3DSymbol ) );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 60, 0 );

  QList<QVector4D> clipPlanesEquationsCylinder = QList<QVector4D>()
                                                 << QVector4D( 0.866025, -0.5, 0, 678.0 )
                                                 << QVector4D( 0.5, 0.866025, 0, 685.0 )
                                                 << QVector4D( -0.5, -0.866025, 0, 680.0 );
  scene->enableClipping( clipPlanesEquationsSphere );

  scene->enableClipping( clipPlanesEquationsCylinder );

  QImage imgCylinder = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "cylinder_rendering_clipping", "cylinder_rendering_clipping", imgCylinder, QString(), 40, QSize( 0, 0 ), 2 );

  scene->disableClipping();

  Qgs3DUtils::captureSceneImage( engine, scene );
  imgCylinder = Qgs3DUtils::captureSceneImage( engine, scene );
  delete scene;
  delete mapSettings;
  QGSVERIFYIMAGECHECK( "cylinder_rendering", "cylinder_rendering", imgCylinder, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testBillboardRendering()
{
  const QgsRectangle fullExtent( 1000, 1000, 2000, 2000 );

  auto layerPointsZ = std::make_unique<QgsVectorLayer>( "PointZ?crs=EPSG:27700", "points Z", "memory" );

  QgsPoint *p1 = new QgsPoint( 1000, 1000, 50 );
  QgsPoint *p2 = new QgsPoint( 1000, 2000, 100 );
  QgsPoint *p3 = new QgsPoint( 2000, 2000, 200 );

  QgsFeature f1( layerPointsZ->fields() );
  QgsFeature f2( layerPointsZ->fields() );
  QgsFeature f3( layerPointsZ->fields() );

  f1.setGeometry( QgsGeometry( p1 ) );
  f2.setGeometry( QgsGeometry( p2 ) );
  f3.setGeometry( QgsGeometry( p3 ) );

  QgsFeatureList featureList;
  featureList << f1 << f2 << f3;
  layerPointsZ->dataProvider()->addFeatures( featureList );

  QgsMarkerSymbol *markerSymbol = static_cast<QgsMarkerSymbol *>( QgsSymbol::defaultSymbol( Qgis::GeometryType::Point ) );
  markerSymbol->setColor( QColor( 255, 0, 0 ) );
  markerSymbol->setSize( 4 );
  QgsSimpleMarkerSymbolLayer *sl = static_cast<QgsSimpleMarkerSymbolLayer *>( markerSymbol->symbolLayer( 0 ) );
  sl->setStrokeColor( QColor( 0, 0, 255 ) );
  sl->setStrokeWidth( 2 );
  QgsPoint3DSymbol *point3DSymbol = new QgsPoint3DSymbol();
  point3DSymbol->setBillboardSymbol( markerSymbol );
  point3DSymbol->setShape( Qgis::Point3DShape::Billboard );

  layerPointsZ->setRenderer3D( new QgsVectorLayer3DRenderer( point3DSymbol ) );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << layerPointsZ.get() );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  // look from the top
  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 0, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "billboard_rendering_1", "billboard_rendering_1", img, QString(), 40, QSize( 0, 0 ), 2 );

  // more perspective look
  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2500, 45, 45 );

  QImage img2 = Qgs3DUtils::captureSceneImage( engine, scene );
  delete scene;
  delete map;

  QGSVERIFYIMAGECHECK( "billboard_rendering_2", "billboard_rendering_2", img2, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testEpsg4978LineRendering()
{
  QgsProject p;

  QgsCoordinateReferenceSystem newCrs( QStringLiteral( "EPSG:4978" ) );
  p.setCrs( newCrs );

  QgsVectorLayer *layerLines = new QgsVectorLayer( testDataPath( "/3d/earth_size_sphere_4978.gpkg" ), "lines", "ogr" );

  QgsLine3DSymbol *lineSymbol = new QgsLine3DSymbol;
  lineSymbol->setRenderAsSimpleLines( true );
  lineSymbol->setWidth( 2 );
  QgsSimpleLineMaterialSettings matSettings;
  matSettings.setAmbient( Qt::red );
  lineSymbol->setMaterialSettings( matSettings.clone() );
  layerLines->setRenderer3D( new QgsVectorLayer3DRenderer( lineSymbol ) );

  const QgsRectangle fullExtent = layerLines->extent();

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( p.crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << layerLines );
  map->setTerrainRenderingEnabled( false );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setCameraNavigationMode( Qgis::NavigationMode::GlobeTerrainBased );

  // look from the top
  scene->cameraController()->resetGlobe( 8'625'000, 90, 270 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "4978_line_rendering_1", "4978_line_rendering_1", img, QString(), 40, QSize( 0, 0 ), 2 );

  // more perspective look
  scene->cameraController()->globeUpdateHeadingAngle( 45 );
  scene->cameraController()->globeUpdatePitchAngle( 45 );

  QImage img2 = Qgs3DUtils::captureSceneImage( engine, scene );
  delete scene;
  delete map;
  delete layerLines;

  QGSVERIFYIMAGECHECK( "4978_line_rendering_2", "4978_line_rendering_2", img2, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testFilteredFlatTerrain()
{
  QgsRectangle fullExtent = mLayerDtm->extent();
  // Set the extent to have width > height
  fullExtent.setYMaximum( fullExtent.yMaximum() - fullExtent.height() / 3 );
  fullExtent.setYMinimum( fullExtent.yMinimum() + fullExtent.height() / 3 );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerRgb );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  // look from the top
  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2000, 0, 0 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "flat_terrain_filtered_1", "flat_terrain_filtered_1", img, QString(), 40, QSize( 0, 0 ), 2 );

  // Now set the extent to have height > width and redo
  fullExtent = mLayerDtm->extent();
  fullExtent.setXMaximum( fullExtent.xMaximum() - fullExtent.width() / 3 );
  fullExtent.setXMinimum( fullExtent.xMinimum() + fullExtent.width() / 3 );
  map->setExtent( fullExtent );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2000, 0, 0 );

  QImage img2 = Qgs3DUtils::captureSceneImage( engine, scene );

  delete scene;
  delete map;

  QGSVERIFYIMAGECHECK( "flat_terrain_filtered_2", "flat_terrain_filtered_2", img2, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testFilteredDemTerrain()
{
  QgsRectangle fullExtent = mLayerDtm->extent();
  // Set the extent to have width > height
  fullExtent.setYMaximum( fullExtent.yMaximum() - fullExtent.height() / 3 );
  fullExtent.setYMinimum( fullExtent.yMinimum() + fullExtent.height() / 3 );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerRgb );

  QgsDemTerrainSettings *demTerrainSettings = new QgsDemTerrainSettings;
  demTerrainSettings->setLayer( mLayerDtm );
  demTerrainSettings->setVerticalScale( 3 );
  map->setTerrainSettings( demTerrainSettings );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2000, 60, 225 );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );

  QImage img1 = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "dem_terrain_filtered_1", "dem_terrain_filtered_1", img1, QString(), 40, QSize( 0, 0 ), 2 );

  // Now set the extent to have height > width and redo
  fullExtent = mLayerDtm->extent();
  fullExtent.setXMaximum( fullExtent.xMaximum() - fullExtent.width() / 3 );
  fullExtent.setXMinimum( fullExtent.xMinimum() + fullExtent.width() / 3 );
  map->setExtent( fullExtent );
  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 2000, 60, 45 );

  QImage img2 = Qgs3DUtils::captureSceneImage( engine, scene );

  delete scene;
  delete map;

  QGSVERIFYIMAGECHECK( "dem_terrain_filtered_2", "dem_terrain_filtered_2", img2, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testFilteredExtrudedPolygons()
{
  const QgsRectangle fullExtent = QgsRectangle( 321720, 129190, 322560, 130060 );

  Qgs3DMapSettings *map = new Qgs3DMapSettings;
  map->setCrs( mProject->crs() );
  map->setExtent( fullExtent );
  map->setLayers( QList<QgsMapLayer *>() << mLayerBuildings << mLayerRgb );
  QgsPointLightSettings defaultLight;
  defaultLight.setIntensity( 0.5 );
  defaultLight.setPosition( QgsVector3D( 0, 0, 1000 ) );
  map->setLightSources( { defaultLight.clone() } );

  QgsFlatTerrainGenerator *flatTerrain = new QgsFlatTerrainGenerator;
  flatTerrain->setCrs( map->crs(), map->transformContext() );
  map->setTerrainGenerator( flatTerrain );

  QgsOffscreen3DEngine engine;
  Qgs3DMapScene *scene = new Qgs3DMapScene( *map, &engine );
  engine.setRootEntity( scene );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, -250, 0 ), 1500, 45, 0 );

  QgsPhongMaterialSettings materialSettings;
  materialSettings.setAmbient( Qt::lightGray );
  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );
  QgsVectorLayer3DRenderer *renderer3d = new QgsVectorLayer3DRenderer( symbol3d );
  mLayerBuildings->setRenderer3D( renderer3d );

  // When running the test on Travis, it would initially return empty rendered image.
  // Capturing the initial image and throwing it away fixes that. Hopefully we will
  // find a better fix in the future.
  Qgs3DUtils::captureSceneImage( engine, scene );
  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );

  delete scene;
  delete map;

  QGSVERIFYIMAGECHECK( "polygon3d_extrusion_filtered", "polygon3d_extrusion_filtered", img, QString(), 40, QSize( 0, 0 ), 2 );
}

void TestQgs3DRendering::testAmbientOcclusion()
{
  // =============================================
  // =========== creating Qgs3DMapSettings
  QgsRasterLayer *layerDtm = new QgsRasterLayer( testDataPath( "/3d/dtm.tif" ), "dtm", "gdal" );
  QVERIFY( layerDtm->isValid() );

  const QgsRectangle fullExtent = layerDtm->extent();

  QgsProject project;
  project.setCrs( layerDtm->crs() );
  project.addMapLayer( layerDtm );

  Qgs3DMapSettings mapSettings;
  mapSettings.setCrs( project.crs() );
  mapSettings.setExtent( fullExtent );
  mapSettings.setLayers( { layerDtm, mLayerBuildings } );

  mapSettings.setTransformContext( project.transformContext() );
  mapSettings.setPathResolver( project.pathResolver() );
  mapSettings.setMapThemeCollection( project.mapThemeCollection() );

  QgsDemTerrainSettings *demTerrainSettings = new QgsDemTerrainSettings;
  demTerrainSettings->setLayer( layerDtm );
  demTerrainSettings->setVerticalScale( 3 );
  mapSettings.setTerrainSettings( demTerrainSettings );

  QgsPointLightSettings defaultPointLight;
  defaultPointLight.setPosition( QgsVector3D( 0, 0, 400 ) );
  defaultPointLight.setConstantAttenuation( 0 );
  mapSettings.setLightSources( { defaultPointLight.clone() } );
  mapSettings.setOutputDpi( 92 );

  QgsPhongMaterialSettings materialSettings;
  materialSettings.setAmbient( Qt::lightGray );
  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol;
  symbol3d->setMaterialSettings( materialSettings.clone() );
  symbol3d->setExtrusionHeight( 10.f );
  symbol3d->setAltitudeClamping( Qgis::AltitudeClamping::Relative );
  QgsVectorLayer3DRenderer *renderer3d = new QgsVectorLayer3DRenderer( symbol3d );
  mLayerBuildings->setRenderer3D( renderer3d );

  // =========== creating Qgs3DMapScene
  QPoint winSize = QPoint( 640, 480 ); // default window size

  QgsOffscreen3DEngine engine;
  engine.setSize( QSize( winSize.x(), winSize.y() ) );
  Qgs3DMapScene *scene = new Qgs3DMapScene( mapSettings, &engine );
  engine.setRootEntity( scene );

  // =========== set camera position
  scene->cameraController()->setLookingAtPoint( QVector3D( 0, 0, 0 ), 400, 50, 10 );

  // =========== set AO to OFF
  QgsAmbientOcclusionSettings aoSettings = mapSettings.ambientOcclusionSettings();
  aoSettings.setEnabled( false );
  mapSettings.setAmbientOcclusionSettings( aoSettings );

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );
  QString actualFG = engine.dumpFrameGraph();
  QGSCOMPARELONGSTR( "ambient_occlusion_1", "framegraph.txt", actualFG.toUtf8() );
  QGSVERIFYIMAGECHECK( "ambient_occlusion_1", "ambient_occlusion_1", img, QString(), 40, QSize( 0, 0 ), 15 );

  // =========== set AO to ON
  aoSettings.setEnabled( true );
  mapSettings.setAmbientOcclusionSettings( aoSettings );

  img = Qgs3DUtils::captureSceneImage( engine, scene );
  actualFG = engine.dumpFrameGraph();
  // the framegraph dump is currently the same when enabled or disabled
  QGSCOMPARELONGSTR( "ambient_occlusion_2", "framegraph.txt", actualFG.toUtf8() );
  QGSVERIFYIMAGECHECK( "ambient_occlusion_2", "ambient_occlusion_2", img, QString(), 40, QSize( 0, 0 ), 15 );

  delete scene;
  mapSettings.setLayers( {} );
}

void TestQgs3DRendering::testDepthBuffer()
{
  // =============================================
  // =========== creating Qgs3DMapSettings
  QgsRasterLayer *layerDtm = new QgsRasterLayer( testDataPath( "/3d/dtm.tif" ), "dtm", "gdal" );
  QVERIFY( layerDtm->isValid() );
  QgsProject project;
  project.setCrs( layerDtm->crs() );
  project.addMapLayer( layerDtm );

  const QgsRectangle fullExtent = layerDtm->extent();

  Qgs3DMapSettings mapSettings;
  mapSettings.setCrs( project.crs() );
  mapSettings.setExtent( fullExtent );
  mapSettings.setLayers( { layerDtm } );

  mapSettings.setTransformContext( project.transformContext() );
  mapSettings.setPathResolver( project.pathResolver() );
  mapSettings.setMapThemeCollection( project.mapThemeCollection() );

  QgsDemTerrainSettings *demTerrainSettings = new QgsDemTerrainSettings;
  demTerrainSettings->setLayer( layerDtm );
  demTerrainSettings->setVerticalScale( 3 );
  mapSettings.setTerrainSettings( demTerrainSettings );

  QgsPointLightSettings defaultPointLight;
  defaultPointLight.setPosition( QgsVector3D( 0, 1000, 0 ) );
  defaultPointLight.setConstantAttenuation( 0 );
  mapSettings.setLightSources( { defaultPointLight.clone() } );
  mapSettings.setOutputDpi( 92 );

  // =========== creating Qgs3DMapScene
  QPoint winSize = QPoint( 640, 480 ); // default window size
  QPoint midPos = winSize / 2;

  QgsOffscreen3DEngine engine;
  engine.setSize( QSize( winSize.x(), winSize.y() ) );
  Qgs3DMapScene *scene = new Qgs3DMapScene( mapSettings, &engine );
  engine.setRootEntity( scene );

  // =========== set camera position
  scene->cameraController()->setLookingAtPoint( QVector3D( 0, 0, 0 ), 1500, 40.0, -10.0 );

  // =============================================
  // =========== TEST DEPTH

  // retrieve 3D depth image
  QImage depthImage = Qgs3DUtils::captureSceneDepthBuffer( engine, scene );
  QImage grayImage = convertDepthImageToGrayscaleImage( depthImage );
  QGSVERIFYIMAGECHECK( "depth_retrieve_image", "depth_retrieve_image", grayImage, QString(), 5, QSize( 0, 0 ), 2 );

  // =========== TEST WHEEL ZOOM
  QVector3D startPos = scene->cameraController()->camera()->position();
  // set cameraController mouse pos to middle screen
  QMouseEvent mouseEvent( QEvent::MouseButtonPress, QPointF( midPos.x(), midPos.y() ), Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier );
  scene->cameraController()->onMousePressed( new Qt3DInput::QMouseEvent( mouseEvent ) );

  // Check first wheel action
  QWheelEvent wheelEvent( midPos, midPos, QPoint(), QPoint( 0, 120 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false, Qt::MouseEventSynthesizedByApplication );
  scene->cameraController()->onWheel( new Qt3DInput::QWheelEvent( wheelEvent ) );
  QCOMPARE( scene->cameraController()->mCumulatedWheelY, wheelEvent.angleDelta().y() / 120.0 );
  QCOMPARE( scene->cameraController()->mCameraBefore->viewCenter(), scene->cameraController()->cameraPose().centerPoint().toVector3D() );

  depthImage = Qgs3DUtils::captureSceneDepthBuffer( engine, scene );
  grayImage = convertDepthImageToGrayscaleImage( depthImage );
  QGSVERIFYIMAGECHECK( "depth_wheel_action_1", "depth_wheel_action_1", grayImage, QString(), 5, QSize( 0, 0 ), 2 );

  scene->cameraController()->depthBufferCaptured( depthImage );
  Qgs3DUtils::waitForFrame( engine, scene );

  QGSCOMPARENEARVECTOR3D( scene->cameraController()->mZoomPoint, QVector3D( -32.7, -185.5, 224.6 ), 1.0 );
  QGSCOMPARENEARVECTOR3D( scene->cameraController()->cameraPose().centerPoint(), QVector3D( -6.5, -37.1, 44.9 ), 1.0 );
  QGSCOMPARENEAR( scene->cameraController()->cameraPose().distanceFromCenterPoint(), 1200, 1.0 );
  QCOMPARE( scene->cameraController()->mCumulatedWheelY, 0 );

  // Checking second wheel action
  QWheelEvent wheelEvent2( midPos, midPos, QPoint(), QPoint( 0, 120 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false, Qt::MouseEventSynthesizedByApplication );
  scene->cameraController()->onWheel( new Qt3DInput::QWheelEvent( wheelEvent2 ) );
  QCOMPARE( scene->cameraController()->mCumulatedWheelY, wheelEvent2.angleDelta().y() / 120.0 );
  QCOMPARE( scene->cameraController()->mCameraBefore->viewCenter(), scene->cameraController()->cameraPose().centerPoint().toVector3D() );

  depthImage = Qgs3DUtils::captureSceneDepthBuffer( engine, scene );
  grayImage = convertDepthImageToGrayscaleImage( depthImage );
  QGSVERIFYIMAGECHECK( "depth_wheel_action_2", "depth_wheel_action_2", grayImage, QString(), 5, QSize( 0, 0 ), 2 );

  scene->cameraController()->depthBufferCaptured( depthImage );
  Qgs3DUtils::waitForFrame( engine, scene );

  QGSCOMPARENEARVECTOR3D( scene->cameraController()->mZoomPoint, QVector3D( -32.5, -184.7, 223.5 ), 1.0 );
  QGSCOMPARENEARVECTOR3D( scene->cameraController()->cameraPose().centerPoint(), QVector3D( -11.7, -66.6, 80.6 ), 1.0 );
  QGSCOMPARENEAR( scene->cameraController()->cameraPose().distanceFromCenterPoint(), 960, 1.0 );
  QCOMPARE( scene->cameraController()->mCumulatedWheelY, 0 );

  // Checking third wheel action
  QWheelEvent wheelEvent3( midPos, midPos, QPoint(), QPoint( 0, 480 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false, Qt::MouseEventSynthesizedByApplication );
  scene->cameraController()->onWheel( new Qt3DInput::QWheelEvent( wheelEvent3 ) );
  QCOMPARE( scene->cameraController()->mCumulatedWheelY, wheelEvent3.angleDelta().y() / 120.0 );
  QCOMPARE( scene->cameraController()->mCameraBefore->viewCenter(), scene->cameraController()->cameraPose().centerPoint().toVector3D() );

  depthImage = Qgs3DUtils::captureSceneDepthBuffer( engine, scene );
  grayImage = convertDepthImageToGrayscaleImage( depthImage );
  QGSVERIFYIMAGECHECK( "depth_wheel_action_3", "depth_wheel_action_3", grayImage, QString(), 5, QSize( 0, 0 ), 2 );

  scene->cameraController()->depthBufferCaptured( depthImage );
  Qgs3DUtils::waitForFrame( engine, scene );

  QGSCOMPARENEARVECTOR3D( scene->cameraController()->mZoomPoint, QVector3D( -32.4, -184.1, 222.8 ), 1.0 );
  QGSCOMPARENEARVECTOR3D( scene->cameraController()->cameraPose().centerPoint(), QVector3D( -24.0, -136.0, 164.6 ), 1.0 );
  QGSCOMPARENEAR( scene->cameraController()->cameraPose().distanceFromCenterPoint(), 393.216, 0.1 );
  QCOMPARE( scene->cameraController()->mCumulatedWheelY, 0 );

  // Checking fourth wheel action
  QWheelEvent wheelEvent4( midPos, midPos, QPoint(), QPoint( 0, 120 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false, Qt::MouseEventSynthesizedByApplication );
  scene->cameraController()->onWheel( new Qt3DInput::QWheelEvent( wheelEvent4 ) );
  QCOMPARE( scene->cameraController()->mCumulatedWheelY, wheelEvent4.angleDelta().y() / 120.0 );
  QCOMPARE( scene->cameraController()->mCameraBefore->viewCenter(), scene->cameraController()->cameraPose().centerPoint().toVector3D() );

  depthImage = Qgs3DUtils::captureSceneDepthBuffer( engine, scene );
  grayImage = convertDepthImageToGrayscaleImage( depthImage );
  QGSVERIFYIMAGECHECK( "depth_wheel_action_4", "depth_wheel_action_4", grayImage, QString(), 5, QSize( 0, 0 ), 2 );

  scene->cameraController()->depthBufferCaptured( depthImage );

  QGSCOMPARENEARVECTOR3D( scene->cameraController()->mZoomPoint, QVector3D( -32.3, -183.2, 221.7 ), 1.0 );
  QGSCOMPARENEARVECTOR3D( scene->cameraController()->cameraPose().centerPoint(), QVector3D( -25.7, -145.6, 176.2 ), 1.0 );
  QGSCOMPARENEAR( scene->cameraController()->cameraPose().distanceFromCenterPoint(), 314.6, 0.1 );
  QCOMPARE( scene->cameraController()->mCumulatedWheelY, 0 );

  // Checking camera position
  QVector3D diff = scene->cameraController()->camera()->position() - startPos;
  QGSCOMPARENEARVECTOR3D( diff, QVector3D( 106, 605, -732 ), 3.0 );

  delete scene;
  mapSettings.setLayers( {} );
}

void TestQgs3DRendering::testDebugMap()
{
  // =============================================
  // =========== creating Qgs3DMapSettings
  QgsRasterLayer *layerDtm = new QgsRasterLayer( testDataPath( "/3d/dtm.tif" ), "dtm", "gdal" );
  QVERIFY( layerDtm->isValid() );

  const QgsRectangle fullExtent = layerDtm->extent();

  QgsProject project;
  project.addMapLayer( layerDtm );

  Qgs3DMapSettings mapSettings;
  mapSettings.setCrs( project.crs() );
  mapSettings.setExtent( fullExtent );
  mapSettings.setLayers( { layerDtm, mLayerBuildings } );

  mapSettings.setTransformContext( project.transformContext() );
  mapSettings.setPathResolver( project.pathResolver() );
  mapSettings.setMapThemeCollection( project.mapThemeCollection() );

  QgsDirectionalLightSettings defaultPointLight;
  mapSettings.setLightSources( { defaultPointLight.clone() } );
  mapSettings.setOutputDpi( 92 );

  QgsShadowSettings shadowSettings = mapSettings.shadowSettings();
  shadowSettings.setRenderShadows( true );
  shadowSettings.setSelectedDirectionalLight( 0 );
  shadowSettings.setMaximumShadowRenderingDistance( 2500 );
  mapSettings.setShadowSettings( shadowSettings );

  // =========== creating Qgs3DMapScene
  QPoint winSize = QPoint( 640, 480 ); // default window size

  QgsOffscreen3DEngine engine;
  engine.setSize( QSize( winSize.x(), winSize.y() ) );
  Qgs3DMapScene *scene = new Qgs3DMapScene( mapSettings, &engine );
  engine.setRootEntity( scene );

  // =========== set camera position
  scene->cameraController()->setLookingAtPoint( QVector3D( 0, 0, 0 ), 2000, 40.0, -10.0 );

  // =========== activate debug depth map
  mapSettings.setDebugDepthMapSettings( true, Qt::Corner::BottomRightCorner, 0.5 );

  // force QT3D backend/frontend synchronization
  {
    scene->cameraController()->setLookingAtPoint( QVector3D( 0, 0, 0 ), 2005, 40.0, -10.0 );
    Qgs3DUtils::captureSceneImage( engine, scene );
    scene->cameraController()->setLookingAtPoint( QVector3D( 0, 0, 0 ), 2000, 40.0, -10.0 );
  }

  QImage img = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "debug_map_1", "debug_map_1", img, QString(), 100, QSize( 0, 0 ), 15 );

#if ( QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 ) ) // shadows do not work for QT6 see: https://github.com/qgis/QGIS/issues/58184
  // =========== activate debug shadow map
  mapSettings.setDebugShadowMapSettings( true, Qt::Corner::TopLeftCorner, 0.5 );

  // force QT3D backend/frontend synchronization
  {
    scene->cameraController()->setLookingAtPoint( QVector3D( 0, 0, 0 ), 2005, 40.0, -10.0 );
    Qgs3DUtils::captureSceneImage( engine, scene );
    scene->cameraController()->setLookingAtPoint( QVector3D( 0, 0, 0 ), 2000, 40.0, -10.0 );
  }

  img = Qgs3DUtils::captureSceneImage( engine, scene );
  QGSVERIFYIMAGECHECK( "debug_map_2", "debug_map_2", img, QString(), 100, QSize( 0, 0 ), 15 );
#endif

  delete scene;
  mapSettings.setLayers( {} );
}

void TestQgs3DRendering::do3DSceneExport( const QString &testName, int zoomLevelsCount, int expectedObjectCount, int expectedFeatureCount, int maxFaceCount, Qgs3DMapScene *scene, QgsVectorLayer *layerPoly, QgsOffscreen3DEngine *engine, QgsTerrainEntity *terrainEntity )
{
  // 3d renderer must be replaced to have the tiling updated
  QgsVectorLayer3DRenderer *renderer3d = dynamic_cast<QgsVectorLayer3DRenderer *>( layerPoly->renderer3D() );
  QgsVectorLayer3DRenderer *newRenderer3d = new QgsVectorLayer3DRenderer( renderer3d->symbol()->clone() );
  QgsVectorLayer3DTilingSettings tilingSettings;
  tilingSettings.setZoomLevelsCount( zoomLevelsCount );
  tilingSettings.setShowBoundingBoxes( true );
  newRenderer3d->setTilingSettings( tilingSettings );
  layerPoly->setRenderer3D( newRenderer3d );

  Qgs3DUtils::captureSceneImage( *engine, scene );

  Qgs3DSceneExporter exporter;
  exporter.setTerrainResolution( 128 );
  exporter.setSmoothEdges( false );
  exporter.setExportNormals( true );
  exporter.setExportTextures( false );
  exporter.setTerrainTextureResolution( 512 );
  exporter.setScale( 1.0 );

  QVERIFY( exporter.parseVectorLayerEntity( scene->layerEntity( layerPoly ), layerPoly ) );
  if ( terrainEntity )
    exporter.parseTerrain( terrainEntity, "DEM_Tile" );

  QString objFileName = QString( "%1-%2" ).arg( testName ).arg( zoomLevelsCount );
  const bool saved = exporter.save( objFileName, QDir::tempPath(), 3 );
  QVERIFY( saved );

  int sum = 0;
  for ( auto o : qAsConst( exporter.mObjects ) )
  {
    if ( !terrainEntity ) // not comptabible with terrain entity
      QVERIFY( o->indexes().size() * 3 <= o->vertexPosition().size() );
    sum += o->indexes().size();
  }

  QCOMPARE( sum, maxFaceCount );
  QCOMPARE( exporter.mExportedFeatureIds.size(), expectedFeatureCount );
  QCOMPARE( exporter.mObjects.size(), expectedObjectCount );

  QFile file( QString( "%1/%2.obj" ).arg( QDir::tempPath(), objFileName ) );
  file.open( QIODevice::ReadOnly | QIODevice::Text );
  QTextStream fileStream( &file );

  if ( !terrainEntity ) // dump with terrain are too big to stay in GIT
    QGSCOMPARELONGSTR( testName.toStdString().c_str(), QString( "%1.obj" ).arg( objFileName ), fileStream.readAll().toUtf8() );
}

void TestQgs3DRendering::test3DSceneExporter()
{
  // =============================================
  // =========== creating Qgs3DMapSettings
  QgsVectorLayer *layerPoly = new QgsVectorLayer( testDataPath( "/3d/polygons.gpkg.gz" ), "polygons", "ogr" );
  QVERIFY( layerPoly->isValid() );

  const QgsRectangle fullExtent = layerPoly->extent();

  // =========== create polygon 3D renderer
  QgsPolygon3DSymbol *symbol3d = new QgsPolygon3DSymbol();
  symbol3d->setExtrusionHeight( 10.f );
  QgsPhongMaterialSettings materialSettings;
  materialSettings.setAmbient( Qt::lightGray );
  symbol3d->setMaterialSettings( materialSettings.clone() );

  QgsVectorLayer3DRenderer *renderer3d = new QgsVectorLayer3DRenderer( symbol3d );
  layerPoly->setRenderer3D( renderer3d );

  QgsProject project;
  project.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 3857 ) );
  project.addMapLayer( layerPoly );

  // =========== create scene 3D settings
  Qgs3DMapSettings mapSettings;
  mapSettings.setCrs( project.crs() );
  mapSettings.setExtent( fullExtent );
  mapSettings.setLayers( { layerPoly } );

  mapSettings.setTransformContext( project.transformContext() );
  mapSettings.setPathResolver( project.pathResolver() );
  mapSettings.setMapThemeCollection( project.mapThemeCollection() );
  mapSettings.setOutputDpi( 92 );

  // =========== creating Qgs3DMapScene
  QPoint winSize = QPoint( 640, 480 ); // default window size

  QgsOffscreen3DEngine engine;
  engine.setSize( QSize( winSize.x(), winSize.y() ) );
  Qgs3DMapScene *scene = new Qgs3DMapScene( mapSettings, &engine );

  scene->cameraController()->setLookingAtPoint( QgsVector3D( 0, 0, 0 ), 7000, 20.0, -10.0 );
  engine.setRootEntity( scene );

  const int nbFaces = 165;
  const int nbFeat = 3;

  // =========== check with 1 big tile ==> 1 exported object
  do3DSceneExport( "scene_export", 1, 1, nbFeat, nbFaces, scene, layerPoly, &engine );
  // =========== check with 4 tiles ==> 1 exported objects
  do3DSceneExport( "scene_export", 2, 1, nbFeat, nbFaces, scene, layerPoly, &engine );
  // =========== check with 9 tiles ==> 3 exported objects
  do3DSceneExport( "scene_export", 3, 3, nbFeat, nbFaces, scene, layerPoly, &engine );
  // =========== check with 16 tiles ==> 3 exported objects
  do3DSceneExport( "scene_export", 4, 3, nbFeat, nbFaces, scene, layerPoly, &engine );
  // =========== check with 25 tiles ==> 3 exported objects
  do3DSceneExport( "scene_export", 5, 3, nbFeat, nbFaces, scene, layerPoly, &engine );

  delete scene;
  mapSettings.setLayers( {} );
}

void TestQgs3DRendering::test3DSceneExporterBig()
{
  // In Qt 6, this test does not work on CI
  // because somewhere after 10K lines, one of the values is -0.304 instead of -0.305
#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
  if ( QgsTest::isCIRun() )
  {
    QSKIP( "fails on CI" );
  }
#endif

  // =============================================
  // =========== creating Qgs3DMapSettings
  QgsRasterLayer *layerDtm = new QgsRasterLayer( testDataPath( "/3d/dtm.tif" ), "dtm", "gdal" );
  QVERIFY( layerDtm->isValid() );

  const QgsRectangle fullExtent = layerDtm->extent();

  QgsProject project;
  project.setCrs( layerDtm->crs() );
  project.addMapLayer( layerDtm );

  Qgs3DMapSettings mapSettings;
  mapSettings.setCrs( project.crs() );
  mapSettings.setExtent( fullExtent );
  mapSettings.setLayers( { layerDtm, mLayerBuildings } );

  mapSettings.setTransformContext( project.transformContext() );
  mapSettings.setPathResolver( project.pathResolver() );
  mapSettings.setMapThemeCollection( project.mapThemeCollection() );

  QgsDemTerrainSettings *demTerrainSettings = new QgsDemTerrainSettings;
  demTerrainSettings->setLayer( layerDtm );
  demTerrainSettings->setVerticalScale( 3 );
  mapSettings.setTerrainSettings( demTerrainSettings );

  QgsPointLightSettings defaultPointLight;
  defaultPointLight.setPosition( QgsVector3D( 0, 400, 0 ) );
  defaultPointLight.setConstantAttenuation( 0 );
  mapSettings.setLightSources( { defaultPointLight.clone() } );
  mapSettings.setOutputDpi( 92 );

  // =========== creating Qgs3DMapScene
  QPoint winSize = QPoint( 640, 480 ); // default window size

  QgsOffscreen3DEngine engine;
  engine.setSize( QSize( winSize.x(), winSize.y() ) );
  Qgs3DMapScene *scene = new Qgs3DMapScene( mapSettings, &engine );
  engine.setRootEntity( scene );

  // =========== set camera position
  scene->cameraController()->setLookingAtPoint( QVector3D( 0, 0, 0 ), 1500, 40.0, -10.0 );

  const int nbFaces = 19869;
  const int nbFeat = 401;

  // =========== check with 1 big tile ==> 1 exported object
  do3DSceneExport( "big_scene_export", 1, 1, nbFeat, nbFaces, scene, mLayerBuildings, &engine );
  // =========== check with 4 tiles ==> 4 exported objects
  do3DSceneExport( "big_scene_export", 2, 4, nbFeat, nbFaces, scene, mLayerBuildings, &engine );
  // =========== check with 9 tiles ==> 14 exported objects
  do3DSceneExport( "big_scene_export", 3, 14, nbFeat, nbFaces, scene, mLayerBuildings, &engine );
  // =========== check with 16 tiles ==> 32 exported objects
  do3DSceneExport( "big_scene_export", 4, 32, nbFeat, nbFaces, scene, mLayerBuildings, &engine );
  // =========== check with 25 tiles ==> 70 exported objects
  do3DSceneExport( "big_scene_export", 5, 70, nbFeat, nbFaces, scene, mLayerBuildings, &engine );

  // =========== check with 25 tiles + terrain ==> 70+1 exported objects
  do3DSceneExport( "terrain_scene_export", 5, 71, nbFeat, 119715, scene, mLayerBuildings, &engine, scene->terrainEntity() );

  delete scene;
  mapSettings.setLayers( {} );
}


QGSTEST_MAIN( TestQgs3DRendering )
#include "testqgs3drendering.moc"
