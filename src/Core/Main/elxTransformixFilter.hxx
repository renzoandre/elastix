/*=========================================================================
 *
 *  Copyright UMC Utrecht and contributors
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#ifndef elxTransformixFilter_hxx
#define elxTransformixFilter_hxx

namespace elastix
{

template< typename TInputImage  >
TransformixFilter< TInputImage >
::TransformixFilter( void )
{
  this->AddRequiredInputName( "TransformParameterObject" );

  this->SetPrimaryInputName( "InputImage" );
  this->SetPrimaryOutputName( "ResultImage" );

  this->SetInput( "InputImage", TInputImage::New() );

  this->m_InputPointSetFileName = std::string();
  this->ComputeSpatialJacobianOff();
  this->ComputeDeterminantOfSpatialJacobianOff();
  this->ComputeDeformationFieldOff();

  this->m_OutputDirectory = std::string();
  this->m_LogFileName = std::string();

  this->LogToConsoleOff();
  this->LogToFileOff();
}

template< typename TInputImage >
void
TransformixFilter< TInputImage >
::GenerateData( void )
{
  // Assert that at least one output has been requested
  if( this->IsEmpty( static_cast< TInputImage* >( this->GetInput( "InputImage" ) ) ) &&
      this->GetInputPointSetFileName().empty() &&
      !this->GetComputeSpatialJacobian() &&
      !this->GetComputeDeterminantOfSpatialJacobian() &&
      !this->GetComputeDeformationField() )
  {
    itkExceptionMacro( "Expected at least one of SetInputImage(\"path/to/image\"), ComputeSpatialJacobianOn(), "
                    << "ComputeDeterminantOfSpatialJacobianOn(), ComputeDeformationFieldOn() or "
                    << "SetInputPointSetFileName(\"path/to/points\") or to bet set." );
  }

  // TODO: patch upstream transformix to save all outputs as data objects
  if( ( this->GetComputeSpatialJacobian() ||
        this->GetComputeDeterminantOfSpatialJacobian() ||
        this->GetComputeDeformationField() ||
        !this->GetInputPointSetFileName().empty() ||
        this->GetLogToFile() ) &&
      this->GetOutputDirectory().empty() )
  {
    this->SetOutputDirectory( "." );
  }

  // Check if output directory exists
  if( ( this->GetComputeSpatialJacobian() ||
        this->GetComputeDeterminantOfSpatialJacobian() ||
        this->GetComputeDeformationField() ||
        !this->GetInputPointSetFileName().empty() ||
        this->GetLogToFile() ) &&
      !itksys::SystemTools::FileExists( this->GetOutputDirectory() ) )
  {
    itkExceptionMacro( "Output directory \"" << this->GetOutputDirectory() << "\" does not exist." )
  }

  // Transformix uses "-def" for path to point sets AND as flag for writing deformation field
  // TODO: Patch upstream transformix to split this into seperate arguments
  if( this->GetComputeDeformationField() && !this->GetInputPointSetFileName().empty() )
  {
    itkExceptionMacro( << "For backwards compatibility, only one of ComputeDeformationFieldOn() "
                       << "or SetInputPointSetFileName() can be active at any one time." )
  }

  // Setup argument map which transformix uses internally ito figure out what needs to be done
  ArgumentMapType argumentMap;
  if( this->GetOutputDirectory().empty() ) {
    // There must be an "-out", this is checked later in the code
    argumentMap.insert( ArgumentMapEntryType( "-out", "output_path_not_set" ) );
  }
  else
  {
    if( this->GetOutputDirectory().back() != '/' || this->GetOutputDirectory().back() != '\\' )
    {
      this->SetOutputDirectory( this->GetOutputDirectory() + "/" );
    }

    argumentMap.insert( ArgumentMapEntryType( "-out", this->GetOutputDirectory() ) );
  }

  if( this->GetComputeSpatialJacobian() )
  {
    argumentMap.insert( ArgumentMapEntryType( "-jacmat", "all" ) );
  }

  if( this->GetComputeDeterminantOfSpatialJacobian() )
  {
    argumentMap.insert( ArgumentMapEntryType( "-jac", "all" ) );
  }

  if( this->GetComputeDeformationField() )
  {
    argumentMap.insert( ArgumentMapEntryType( "-def" , "all" ) );
  }

  if( !this->GetInputPointSetFileName().empty() )
  {
    argumentMap.insert( ArgumentMapEntryType( "-def", this->GetInputPointSetFileName() ) );
  }

  // Setup xout
  std::string logFileName;
  if( this->GetLogToFile() )
  {
    if( this->GetLogFileName().empty() )
    {
      logFileName = this->GetOutputDirectory() + "transformix.log";
    }
    else
    {
      if( this->GetOutputDirectory()[ this->GetOutputDirectory().size()-1 ] != '/' || this->GetOutputDirectory()[ this->GetOutputDirectory().size()-1 ] != '\\' )
      {
        this->SetOutputDirectory( this->GetOutputDirectory() + "/" );
      }
      logFileName = this->GetOutputDirectory() + this->GetLogFileName();
    }
  }

  if( elx::xoutSetup( logFileName.c_str(), this->GetLogToFile(), this->GetLogToConsole() ) )
  {
    itkExceptionMacro( "Error while setting up xout" );
  }

  // Instantiate transformix
  TransformixMainPointer transformix = TransformixMainType::New();

  // Setup transformix for warping input image if given
  DataObjectContainerPointer inputImageContainer = 0;
  DataObjectContainerPointer resultImageContainer = 0;
  if( !this->IsEmpty( static_cast< TInputImage* >( this->GetInput( "InputImage" ) ) ) ) {
    inputImageContainer = DataObjectContainerType::New();
    inputImageContainer->CreateElementAt( 0 ) = this->GetInput( "InputImage" );
    transformix->SetInputImageContainer( inputImageContainer );
    transformix->SetResultImageContainer( resultImageContainer );
  }

  // Get ParameterMap
  ParameterObjectConstPointer transformParameterObject = static_cast< const ParameterObject* >( this->GetInput( "TransformParameterObject" ) );
  ParameterMapVectorType transformParameterMapVector = transformParameterObject->GetParameterMap();

  // Assert user did not set empty parameter map
  if( transformParameterMapVector.size() == 0 )
  {
    itkExceptionMacro( "Empty parameter map in parameter object." );
  }
  
  for( unsigned int i = 0; i < transformParameterMapVector.size(); ++i )
  {
    // Transformix reads type information from parameter files. We set this information automatically and overwrite
    // user settings in case they are incorrect (in which case elastix will segfault or throw exception)
    transformParameterMapVector[ i ][ "FixedInternalImagePixelType" ] = ParameterValueVectorType( 1, PixelTypeName< typename TInputImage::PixelType >::ToString() );
    transformParameterMapVector[ i ][ "FixedImageDimension" ] = ParameterValueVectorType( 1, ParameterObject::ToString( InputImageDimension ) );
    transformParameterMapVector[ i ][ "MovingInternalImagePixelType" ] = ParameterValueVectorType( 1, PixelTypeName< typename TInputImage::PixelType >::ToString() );
    transformParameterMapVector[ i ][ "MovingImageDimension" ] = ParameterValueVectorType( 1, ParameterObject::ToString( InputImageDimension ) );
    transformParameterMapVector[ i ][ "ResultImagePixelType" ] = ParameterValueVectorType( 1, PixelTypeName< typename TInputImage::PixelType >::ToString() );
  }

  // Run transformix
  unsigned int isError = 0;
  try
  {
    isError = transformix->Run( argumentMap, transformParameterMapVector );
  }
  catch( itk::ExceptionObject &e )
  {
    itkExceptionMacro( << "Errors occured during registration: " << e.what() );
  }

  if( isError != 0 )
  {
    itkExceptionMacro( << "Uncought errors occured during registration." );
  }

  // Save result image
  resultImageContainer = transformix->GetResultImageContainer();
  if( resultImageContainer.IsNotNull() && resultImageContainer->Size() > 0 )
  {
    this->GraftOutput( "ResultImage", resultImageContainer->ElementAt( 0 ) );
  }

  // Clean up
  TransformixMainType::UnloadComponents();
}

template< typename TInputImage >
void
TransformixFilter< TInputImage >
::SetInputImage( InputImagePointer inputImage )
{
  this->SetInput( "InputImage", static_cast< itk::DataObject* >( inputImage ) );
}

template< typename TInputImage >
void
TransformixFilter< TInputImage >
::SetTransformParameterObject( ParameterObjectPointer parameterObject )
{
  this->SetInput( "TransformParameterObject", static_cast< itk::DataObject* >( parameterObject ) );
}

template< typename TInputImage >
typename TransformixFilter< TInputImage >::ParameterObjectPointer
TransformixFilter< TInputImage >
::GetTransformParameterObject( void )
{
  return static_cast< ParameterObject* >( this->GetInput( "TransformParameterObject" ) );
}

template< typename TInputImage >
bool
TransformixFilter< TInputImage >
::IsEmpty( InputImagePointer inputImage )
{
  typename TInputImage::RegionType region = inputImage->GetLargestPossibleRegion();
  typename TInputImage::SizeType size = region.GetSize();
  return size[ 0 ] == 0 && size[ 1 ] == 0;
}

} // namespace elx

#endif // elxTransformixFilter_hxx
