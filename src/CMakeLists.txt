add_library(tsunami_core STATIC
    core/File_System.cpp
)
add_library(Tsunami::core ALIAS tsunami_core)

target_include_directories(tsunami_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(tsunami_core PUBLIC cxx_std_20)

tsunami_apply_compiler_warnings(tsunami_core)

add_library(tsunami_fvm STATIC
    fvm/src/FiniteVolumeMesh.cpp
    fvm/src/GreenGaussGradient.cpp
    fvm/src/LinearInterpolationStencil.cpp
    fvm/src/MeshBinding.cpp
    fvm/src/MeshGeometry.cpp
    fvm/src/MeshTopology.cpp
)
add_library(Tsunami::fvm ALIAS tsunami_fvm)
target_include_directories(tsunami_fvm
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fvm/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_fvm PUBLIC cxx_std_20)
target_link_libraries(tsunami_fvm PUBLIC tsunami_core)
tsunami_apply_compiler_warnings(tsunami_fvm)

add_library(tsunami_mesh_gmsh STATIC
    adapters/mesh/gmsh/src/GmshMeshImporter.cpp
)
target_include_directories(tsunami_mesh_gmsh
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/adapters/mesh/gmsh/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_mesh_gmsh PUBLIC cxx_std_20)
target_link_libraries(tsunami_mesh_gmsh PUBLIC tsunami_core tsunami_fvm)
tsunami_apply_compiler_warnings(tsunami_mesh_gmsh)

find_package(nlohmann_json CONFIG REQUIRED)

add_library(tsunami_data STATIC
    data/src/CaseConfiguration.cpp
    data/src/CaseConfigurationParsing.cpp
    data/src/CaseConfigurationSerialisation.cpp
    data/src/CaseConfigurationValidation.cpp
    data/src/DatasetManifest.cpp
    data/src/DatasetManifestParsing.cpp
    data/src/DatasetManifestSerialisation.cpp
    data/src/DatasetManifestValidation.cpp
)
add_library(Tsunami::data ALIAS tsunami_data)
target_include_directories(tsunami_data
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/data/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_data PUBLIC cxx_std_20)
target_link_libraries(tsunami_data
    PUBLIC
        tsunami_core
    PRIVATE
        nlohmann_json::nlohmann_json
)
tsunami_apply_compiler_warnings(tsunami_data)

add_library(tsunami_geo STATIC
    geo/src/ConstructedCorridor.cpp
    geo/src/CorridorConstruction.cpp
    geo/src/CorridorConstructionParsing.cpp
    geo/src/CorridorConstructionRecord.cpp
    geo/src/CorridorConstructionSerialisation.cpp
    geo/src/ConditionedTerrainRaster.cpp
    geo/src/CoordinateTransformation.cpp
    geo/src/CoordinateTransformationPlan.cpp
    geo/src/CoordinateTransformationRecord.cpp
    geo/src/CoordinateTransformationSerialisation.cpp
    geo/src/GeospatialImport.cpp
    geo/src/GeospatialImportRecord.cpp
    geo/src/GeospatialImportSerialisation.cpp
    geo/src/ImportedRaster.cpp
    geo/src/ImportedVector.cpp
    geo/src/SpatialReferenceEvidence.cpp
    geo/src/TerrainConditioning.cpp
    geo/src/TerrainConditioningParsing.cpp
    geo/src/TerrainConditioningRecord.cpp
    geo/src/TerrainConditioningSerialisation.cpp
    geo/src/TerrainResampling.cpp
    geo/src/TerrainTargetGrid.cpp
    geo/src/TransformedVector.cpp
)
add_library(Tsunami::geo ALIAS tsunami_geo)
target_include_directories(tsunami_geo
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/geo/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_geo PUBLIC cxx_std_20)
target_link_libraries(tsunami_geo
    PUBLIC
        tsunami_core
        tsunami_data
    PRIVATE
        nlohmann_json::nlohmann_json
)
tsunami_apply_compiler_warnings(tsunami_geo)

if(TSUNAMI_ENABLE_GEOSPATIAL)
    find_package(GDAL CONFIG REQUIRED)
    add_library(tsunami_geo_gdal STATIC
        geo_gdal/src/GdalEarthquakeDisplacementArtifacts.cpp
        geo_gdal/src/GdalGeospatialImporter.cpp
        geo_gdal/src/GdalConditionedTerrainArtifacts.cpp
        geo_gdal/src/GdalMetadataExtraction.cpp
        geo_gdal/src/GdalRasterImporter.cpp
        geo_gdal/src/GdalTerrainResampler.cpp
        geo_gdal/src/GdalVectorImporter.cpp
    )
    target_include_directories(tsunami_geo_gdal
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/geo_gdal/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/geo_gdal/src
    )
    target_compile_features(tsunami_geo_gdal PUBLIC cxx_std_20)
    target_link_libraries(tsunami_geo_gdal
        PUBLIC
            tsunami_geo
        PRIVATE
            GDAL::GDAL
            nlohmann_json::nlohmann_json
    )
    tsunami_apply_compiler_warnings(tsunami_geo_gdal)

endif()

if(TSUNAMI_ENABLE_CRS_TRANSFORMATIONS)
    find_package(PROJ CONFIG REQUIRED)
    add_library(tsunami_geo_proj STATIC
        geo_proj/src/ProjContext.cpp
        geo_proj/src/ProjCoordinateTransformer.cpp
        geo_proj/src/ProjMetadataExtraction.cpp
        geo_proj/src/ProjMetadataRecord.cpp
        geo_proj/src/ProjOperationSelection.cpp
        geo_proj/src/ProjPointTransformation.cpp
        geo_proj/src/ProjRasterTransformationPlan.cpp
        geo_proj/src/ProjVectorTransformation.cpp
    )
    target_include_directories(tsunami_geo_proj
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/geo_proj/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/geo_proj/src
    )
    target_compile_features(tsunami_geo_proj PUBLIC cxx_std_20)
    target_link_libraries(tsunami_geo_proj
        PUBLIC
            tsunami_geo
        PRIVATE
            PROJ::proj
    )
    tsunami_apply_compiler_warnings(tsunami_geo_proj)
endif()

add_subdirectory(application)

add_library(tsunami_r2d STATIC
    r2d/src/CflTimestep.cpp
    r2d/src/ExplicitIntegration.cpp
    r2d/src/FreeSurfaceElevation.cpp
    r2d/src/ForwardEulerUpdate.cpp
    r2d/src/HydrostaticReconstruction.cpp
    r2d/src/PositivityTimestep.cpp
    r2d/src/RegionalBoundaryCondition.cpp
    r2d/src/RegionalBathymetry.cpp
    r2d/src/RegionalCasePreparation.cpp
    r2d/src/RegionalConservedState.cpp
    r2d/src/RegionalDiagnostics.cpp
    r2d/src/RegionalEarthquakeDiagnostics.cpp
    r2d/src/RegionalEarthquakeInitialisation.cpp
    r2d/src/RegionalExteriorState.cpp
    r2d/src/RegionalGeometryPreflight.cpp
    r2d/src/RegionalTerrainTransfer.cpp
    r2d/src/RegionalRelaxationZone.cpp
    r2d/src/RegionalResidual.cpp
    r2d/src/RegionalResidualEvaluation.cpp
    r2d/src/RegionalSimulationState.cpp
    r2d/src/RegionalSnapshot.cpp
    r2d/src/RegionalSeabedDisplacement.cpp
    r2d/src/RegionalSolveLoop.cpp
    r2d/src/RegionalSourceTerms.cpp
    r2d/src/RegionalSourceTimestep.cpp
    r2d/src/RegionalSourceUpdate.cpp
    r2d/src/RegionalStateCombination.cpp
    r2d/src/RegionalTimeIntegration.cpp
    r2d/src/RusanovFlux.cpp
    r2d/src/ShallowWaterFlux.cpp
    r2d/src/ShallowWaterState.cpp
    r2d/src/WellBalancedResidualEvaluation.cpp
    r2d/src/WetDryUpdate.cpp
)
add_library(Tsunami::r2d ALIAS tsunami_r2d)
target_include_directories(tsunami_r2d
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/r2d/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_r2d PUBLIC cxx_std_20)
target_link_libraries(tsunami_r2d PUBLIC tsunami_core tsunami_fvm tsunami_data tsunami_geo)
if(TSUNAMI_ENABLE_OPENMP)
    target_link_libraries(tsunami_r2d PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(tsunami_r2d PUBLIC TSUNAMI_ENABLE_OPENMP=1)
endif()
tsunami_apply_compiler_warnings(tsunami_r2d)

add_library(tsunami_r2d_io STATIC
    r2d_io/src/RegionalCsvOutputWriter.cpp
)
add_library(Tsunami::r2d_io ALIAS tsunami_r2d_io)
target_include_directories(tsunami_r2d_io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/r2d_io/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_r2d_io PUBLIC cxx_std_20)
target_link_libraries(tsunami_r2d_io PUBLIC tsunami_core tsunami_r2d)
tsunami_apply_compiler_warnings(tsunami_r2d_io)

if(TSUNAMI_ENABLE_GEOSPATIAL)
    add_library(tsunami_r2d_case_runner STATIC
        r2d_case_runner/src/RegionalFileCaseRunner.cpp
    )
    add_library(Tsunami::r2d_case_runner ALIAS tsunami_r2d_case_runner)
    target_include_directories(tsunami_r2d_case_runner
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/r2d_case_runner/include>
            $<INSTALL_INTERFACE:include>
    )
    target_compile_features(tsunami_r2d_case_runner PUBLIC cxx_std_20)
    target_link_libraries(tsunami_r2d_case_runner
        PUBLIC
            tsunami_core
            tsunami_data
            tsunami_geo
            tsunami_fvm
            tsunami_r2d
            tsunami_coupling
        PRIVATE
            tsunami_geo_gdal
            tsunami_mesh_gmsh
            tsunami_r2d_io
            nlohmann_json::nlohmann_json
    )
    tsunami_apply_compiler_warnings(tsunami_r2d_case_runner)
endif()

add_library(tsunami_r2d_benchmarks STATIC
    r2d_benchmarks/src/RegionalBenchmarkCases.cpp
    r2d_benchmarks/src/StructuredTriangularMesh.cpp
)
add_library(Tsunami::r2d_benchmarks ALIAS tsunami_r2d_benchmarks)
target_include_directories(tsunami_r2d_benchmarks
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/r2d_benchmarks/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_r2d_benchmarks PUBLIC cxx_std_20)
target_link_libraries(tsunami_r2d_benchmarks PUBLIC tsunami_core tsunami_fvm tsunami_r2d)
tsunami_apply_compiler_warnings(tsunami_r2d_benchmarks)

add_library(tsunami_l3d INTERFACE)
add_library(Tsunami::l3d ALIAS tsunami_l3d)
target_include_directories(tsunami_l3d
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/l3d/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_l3d INTERFACE cxx_std_20)
target_link_libraries(tsunami_l3d INTERFACE tsunami_core tsunami_data)

add_library(tsunami_coupling INTERFACE)
add_library(Tsunami::coupling ALIAS tsunami_coupling)
target_include_directories(tsunami_coupling
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/coupling/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_coupling INTERFACE cxx_std_20)
target_link_libraries(tsunami_coupling
    INTERFACE
        tsunami_core
        tsunami_data
        tsunami_r2d
        tsunami_l3d
)
