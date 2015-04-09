<?php

/**
 * @file  openAIP Airspace converter
 *
 */
include 'open_aip_airspace.converter.airspace.inc';

class AirspaceConverter
{
  private $srcFormat;
  private $srcString;
  private $maxLat;
  private $minLat;
  private $maxLon;
  private $minLon;

  public $errors;
  public $warnings;
  public $airspaces;

  function __construct()
  {
    $this->srcFormat = "";
    $this->srcString = "";
    $this->errors = "";
    $this->warnings = "";
    $this->maxLat = 90.0;
    $this->minLat = -90.0;
    $this->maxLon = 180.0;
    $this->minLon = -180.0;
    $this->airspaces = array();
  }

  function __destruct()
  {
    unset($this->airspaces);
  }

  function loadFile($srcPath, $srcFormat)
  {
    $this->srcFormat = $srcFormat;
    $this->srcString = "";
    $this->errors = "";
    $this->warnings = "";
    $this->maxLat = 90.0;
    $this->minLat = -90.0;
    $this->maxLon = 180.0;
    $this->minLon = -180.0;

    // open input file
    $handle = fopen($srcPath, "r");
    if ($handle)
    {
      while (($line = fgets($handle, 4096)) !== FALSE)
      {
        $this->srcString .= $line;
      }
    }
    fclose($handle);

    return $this->loadData();
  }

  function loadString($srcString, $srcFormat)
  {
    $this->srcFormat = $srcFormat;
    $this->srcString = $srcString;
    $this->errors = "";
    $this->warnings = "";
    $this->maxLat = 90.0;
    $this->minLat = -90.0;
    $this->maxLon = 180.0;
    $this->minLon = -180.0;

    // an openair string MUST contain a class to start the parser
    // as this function is mainly used to
    if ( (!strncmp($this->srcFormat, "OPENAIR", 7)) && (strpos($this->srcString, "AC") === FALSE) )
    {
      $this->srcString = "AC X\n" . $this->srcString;
    }

    return $this->loadData();
  }

  function getFirstAirspaceWkt()
  {
    if (count($this->airspaces) > 0)
    {
      return $this->airspaces[0]->geometry->toWkt();
    }
    else
    {
      return "";
    }
  }

  function arcRes($radius)
  {
    // max error on arc in nm
    $maxError = 0.005;
    $minArcRes = 0.2;
    $res = (2.0 * acos(($radius - $maxError)/$radius));

    if (($res < $minArcRes) && ($res > 0.0))
    {
      return $res;
    }
    else
    {
      return $minArcRes;
    }
  }

  private function updateAspPath($asp, $currentPath, $pathIsAirway, $airwayWidth, $np)
  {
    if ($pathIsAirway == TRUE)
    {
      $airway = new AipPath(); // "left border"
      $rightPath = new AipPath(); // "right border"

      $startPoint = $currentPath->pathElements[0];

      for ($idx = 1; $idx < $np; $idx++)
      {
        $point = $currentPath->pathElements[$idx];
        $brg = $startPoint->bearingTo($point);

        // if this is the first iteration init last brg
        if ($idx == 1)
        {
          $lastBrg = $brg;
        }

        $angle = ($lastBrg + $brg) * 0.5;

        $alpha = abs($angle - $brg);
        $radius = ($airwayWidth * 0.5) / cos($alpha);

        $leftPoint = $startPoint->pointInDirectionAndDistance($angle - 0.5*M_PI, $radius);
        $airway->appendPoint($leftPoint);
        $rightPoint = $startPoint->pointInDirectionAndDistance($angle + 0.5*M_PI, $radius);
        $rightPath->appendPoint($rightPoint);

        $lastBrg = $brg;
        $startPoint = $point;

        // last iteration
        if ($idx == ($np-1))
        {
          // if first point and last point are identical, close airway
          if (   ($currentPath->pathElements[0]->lat == $point->lat)
              && ($currentPath->pathElements[0]->lon == $point->lon)
              )
          {
            $point = $currentPath->pathElements[1];
            $brg = $startPoint->bearingTo($point);
            $angle = ($lastBrg + $brg) * 0.5;

            $alpha = abs($angle - $brg);
            $radius = ($airwayWidth * 0.5) / cos($alpha);

            $leftPoint = $startPoint->pointInDirectionAndDistance($angle - 0.5*M_PI, $radius);
            $airway->appendPoint($leftPoint);
            $rightPoint = $startPoint->pointInDirectionAndDistance($angle + 0.5*M_PI, $radius);
            $rightPath->appendPoint($rightPoint);

            $airway->pathElements[0] = $leftPoint;
            $rightPath->pathElements[0] = $rightPoint;

            /*
            // try to find out which polygon is inner, which is outer..
            // find center
            $center = new AipPoint();
            for ($idx = 0; $idx < $np; $idx++)
            {
              $center->lat += $currentPath->pathElements[$idx]->lat;
              $center->lon += $currentPath->pathElements[$idx]->lon;
            }
            $center->lat /= $np;
            $center->lon /= $np;

            if ($center->distanceTo($leftPoint) < $center->distanceTo($rightPoint))
            {
              $asp->setInnerPath($airway);
              $asp->setOuterPath($rightPath);
            }
            else
            {
              $asp->setInnerPath($rightPath);
              $asp->setOuterPath($airway);
            }*/
          }
          else
          {
            $leftPoint = $point->pointInDirectionAndDistance($brg - 0.5*M_PI, $airwayWidth * 0.5);
            $airway->appendPoint($leftPoint);
            $rightPoint = $point->pointInDirectionAndDistance($brg + 0.5*M_PI, $airwayWidth * 0.5);
            $rightPath->appendPoint($rightPoint);
          }
        }
      }

      // concat paths opposite site in reverse order..
      for ($idx = $np - 1; $idx >= 0 ; $idx--)
      {
        $airway->appendPoint($rightPath->pathElements[$idx]);
      }
      $airway->closePath();
      $asp->setPath($airway);
    }
    else
    {
      $currentPath->closePath();
      $asp->setPath($currentPath);
    }
  }

  private function createAipPoint( $latdeg, $latmin, $latsec, $latvz,
                                   $londeg, $lonmin, $lonsec, $lonvz)
  {
    $result = new AipPoint();

    $result->lat = $latdeg + $latmin/60.0 + $latsec/3600.0;
    if($latvz == 'S')
    {
      $result->lat *= -1.0;
    }
    $result->lon = $londeg + $lonmin/60.0 + $lonsec/3600.0;
    if($lonvz == 'W')
    {
      $result->lon *= -1.0;
    }

    return $result;
  }

  private function parseCoordPair($dpLine)
  {
    $n = sscanf($dpLine, "%f:%f:%f %c %f:%f:%f %c",
                 $latdeg, $latmin, $latsec, $latvz,
                 $londeg, $lonmin, $lonsec, $lonvz
                 );

    if ($n == 8)
    {
      return $this->createAipPoint($latdeg, $latmin, $latsec, $latvz, $londeg, $lonmin, $lonsec, $lonvz);
    }
    else
    {
      $n = sscanf($dpLine, "%f:%f %c %f:%f %c",
                   $latdeg, $latmin, $latvz,
                   $londeg, $lonmin, $lonvz
                   );

      if ($n == 6)
      {
        return $this->createAipPoint($latdeg, $latmin, 0.0, $latvz, $londeg, $lonmin, 0.0, $lonvz);
      }
      else
      {
        $n = sscanf($dpLine, "%2f%2f%f%c %3f%2f%f%c",
                     $latdeg, $latmin, $latsec, $latvz,
                     $londeg, $lonmin, $lonsec, $lonvz
                     );

        if ($n == 8)
        {
          return $this->createAipPoint($latdeg, $latmin, $latsec, $latvz, $londeg, $lonmin, $lonsec, $lonvz);
        }
      }
    }

    return false;
  }

  private function loadData()
  {
    $this->errors = "";
    $this->warnings = "";

    unset($this->airspaces);
    $this->airspaces = array();

    if (!strncmp($this->srcFormat, "OPENAIP", 7))
    {
      $xml = simplexml_load_string($this->srcString);
      
      foreach ($xml->AIRSPACES->ASP as $aspXml)
      {
        $asp = new Airspace();
        
        $asp->category = $aspXml["CATEGORY"];
        $asp->name = $aspXml->NAME;
        
        // top limit
        $toplimit = $aspXml->ALTLIMIT_TOP;
        $asp->topLimit->refString = $toplimit["REFERENCE"];
        $asp->topLimit->unitString = $toplimit->ALT["UNIT"];
        $asp->topLimit->altString = $toplimit->ALT;

        // bottom limit
        $bottomlimit = $aspXml->ALTLIMIT_BOTTOM;
        $asp->bottomLimit->refString = $bottomlimit["REFERENCE"];
        $asp->bottomLimit->unitString = $bottomlimit->ALT["UNIT"];
        $asp->bottomLimit->altString = $bottomlimit->ALT;
        
        // polygon
        $currentPath = new AipPath();
        
        $polygonStr = $aspXml->GEOMETRY->POLYGON;
        $coords = explode(",", $polygonStr);
        
        foreach ($coords as $coord)
        {
          $coord = trim($coord);
          $lonlat = explode(" ", $coord);
          
          $point = new AipPoint();
          $point->lat = $lonlat[1];
          $point->lon = $lonlat[0];
          
          $currentPath->appendPoint($point);
        }
        
        $asp->setPath($currentPath);
        
        // add current airspace to array
        $this->airspaces[] = $asp;
      }
      return TRUE;
    }
    else if (!strncmp($this->srcFormat, "OPENAIR", 7))
    {
      $started = FALSE;
      $writeAsp = FALSE;
      $np = 0;
      $direction = "+";
      $stype = "";
      $airwayWidth = 0.0;
      $pathIsAirway = FALSE;

      $asp = new Airspace();
      $currentPath = new AipPath();
      $koCenter = new AipPoint();
      $arcFound = FALSE;

      $this->maxLat = 90.0;
      $this->minLat = -90.0;
      $this->maxLon = 180.0;
      $this->minLon = -180.0;

      $skip = FALSE;

      $lines = explode("\n", $this->srcString);

      foreach ($lines as $line)
      {
        if (!strncmp($line, "*", 1))
        {
          continue;
        }
        else if (strpos($line, "STREPLA_ADDON BEGIN") !== FALSE)
        {
          $skip = TRUE;
        }
        else if (strpos($line, "STREPLA_ADDON END") !== FALSE)
        {
          $skip = FALSE;
        }

        if ($skip)
        {
          continue;
        }

        // remove all unnecessary spaces
        $line = trim($line);
        $line = str_replace("  ", " ", $line);
        $line = str_replace("  ", " ", $line);
        $line = str_replace("  ", " ", $line);
        $line = str_replace("  ", " ", $line);
        $line = str_replace(" ,", ",", $line);

        // check if next asp has started..
        $nextAsp = FALSE;
        if (!strncmp($line, "AC", 2))
        {
          $nextAsp = TRUE;
        }

        if ((!strncmp($line, "AN", 2)) && ($np > 0))
        {
          $nextAsp = TRUE;
        }

        if ($nextAsp)
        {
          // it can be either the first airspace in file
          if (!$started)
          {
            $started = TRUE;
          }
          // or a subsequent airspace
          else if ($np > 0)
          {
            $writeAsp = TRUE;
          }
        }

        if ($writeAsp)
        {
          $this->updateAspPath($asp, $currentPath, $pathIsAirway, $airwayWidth, $np);

          if (($np < 5) && $arcFound)
          {
            $this->warnings .= "Warning: ".$asp->name." contains only ".$np." points<BR>\n";
          }
          $arcFound = FALSE;

          // add current airspace to array
          $this->airspaces[] = $asp;
          $writeAsp = FALSE;
          $direction = '+';

          $lastCat = $asp->category;
          $asp = new Airspace();
          $currentPath = new AipPath();
          $np = 0;
          $pathIsAirway = FALSE;
          $asp->category = $lastCat;
        }

        // asp class
        if (!strncmp($line, "AC", 2))
        {
          sscanf($line, "AC %s", $stype);
          $line = strtoupper($line);

          if (!strcmp($stype, "CTR"))
          {
            $asp->category = "CTR";
          }
          else if (!strcmp($stype, "FIR"))
          {
            $asp->category = "FIR";
          }
          else if (!strcmp($stype, "UIR"))
          {
            $asp->category = "UIR";
          }
          else if (!strcmp($stype, "R"))
          {
            $asp->category = "RESTRICTED";
          }
          else if (!strcmp($stype, "P"))
          {
            $asp->category = "PROHIBITED";
          }
          else if(!strcmp($stype, "A"))
          {
            $asp->category = "A";
          }
          else if(!strcmp($stype, "B"))
          {
            $asp->category = "B";
          }
          else if(!strcmp($stype, "C"))
          {
            $asp->category = "C";
          }
          else if(!strcmp($stype, "D"))
          {
            $asp->category = "D";
          }
          else if(!strcmp($stype, "E"))
          {
            $asp->category = "E";
          }
          else if(!strcmp($stype, "F"))
          {
            $asp->category = "F";
          }
          else if(!strcmp($stype, "Q"))
          {
            $asp->category = "DANGER";
          }
          else if(!strcmp($stype, "W"))
          {
            $asp->category = "WAVE";
          }
          else if(!strcmp($stype, "GP"))
          {
            $asp->category = "RESTRICTED";
          }
          else if(!strcmp($stype, "TMZ"))
          {
            $asp->category = "TMZ";
          }
          else if(!strcmp($stype, "G"))
          {
            $asp->category = "GLIDING";
          }
          else if(!strcmp($stype, "S"))
          {
            $asp->category = "DROPZONE";
          }
          else if(!strcmp($stype, "RMZ"))
          {
            $asp->category = "RMZ";
          }
          else
          {
            $this->warnings .= "Warning: Unknown airspace category in line: ".$line."<BR>\n";
            $asp->category = "UNKNOWN";
          }
        }

        if ($started)
        {
          if(!strncmp($line, "AN", 2))
          {
            $name = substr($line, 3);
            
            if (!strncmp(substr($name, 0, 3), "RMZ", 3))
            {
              $name = trim(substr($name, 3));
              $asp->category = "RMZ";   
            }
            
            // if category is not unknown
            if(strcmp($asp->category, "UNKNOWN"))
            {
              $asp->name = $name;
            }
            else
            {
              $asp->name = $stype." - ".$name;
            }
          }
          else if ((!strncmp($line, "AH", 2)) || (!strncmp($line, "AL", 2)))
          {
            $line = strtoupper($line);

            $limit = new AipVerticalLimit("XXX");

            $tInt = 0;

            $str = substr($line, 3);

            if (strpos($str, "FL") !== FALSE)
            {

              // "FL195xxx", "FL 195xxx"
              $limit->refString = "STD";
              $limit->unitString = "FL";
              sscanf(substr($str, 2), "%d", $tInt);
              $limit->altString = (string)$tInt;
            }
            else if ((strpos($str, "GND") === 0) || (strpos($str, "SFC") === 0))
            {
              // "GND", "SFC"
              $limit->refString = "GND";
              $limit->altString = "0";
            }
            else if ((strpos($str, "MSL") !== FALSE) || (strpos($str, "ALT") !== FALSE))
            {
              // "2000F MSL", "2000F AMSL", "2000 MSL", "2000 FT AMSL", "1500MSL", "1500ALT"
              $limit->refString = "MSL";
              sscanf($str, "%d", $tInt);

              if ((strpos($str, "m MSL") !== FALSE) || (strpos($str, "M MSL") !== FALSE))
              {
                $tInt = $tInt * 3.2808399;
              }

              $limit->altString = (string)$tInt;
            }
            else
            {
              // "2000F GND", "2000F AGL", "2000AGND", "2000 FT ASFC", "1500GND"
              $limit->refString = "GND";
              sscanf($str, "%d", $tInt);

              if ((strpos($str, "m GND") !== FALSE) || (strpos($str, "M GND") !== FALSE))
              {
                $tInt = $tInt * 3.2808399;
              }

              $limit->altString = (string)$tInt;
            }

            if (!strncmp($line, "AH", 2))
            {
              $asp->topLimit->refString = $limit->refString;
              $asp->topLimit->unitString = $limit->unitString;
              $asp->topLimit->altString = $limit->altString;
            }
            else
            {
              $asp->bottomLimit->refString = $limit->refString;
              $asp->bottomLimit->unitString = $limit->unitString;
              $asp->bottomLimit->altString = $limit->altString;
            }
          }
          else if (!strncmp($line, "DP", 2))
          {
            $point = $this->parseCoordPair(substr($line, 2));
            if ($point !== false)
            {
              $currentPath->appendPoint($point);
              $np++;
            }
          }
          else if (!strncmp($line, "DY", 2))
          {
            $pathIsAirway = TRUE;
            $point = $this->parseCoordPair(substr($line, 2));
            if ($point !== false)
            {
              $currentPath->appendPoint($point);
              $np++;
            }
          }
          else if (!strncmp($line, "V X", 3))
          {
            $arcFound = TRUE;
            $point = $this->parseCoordPair(substr($line, 4));
            if ($point !== false)
            {
              $koCenter->lat = $point->lat;
              $koCenter->lon = $point->lon;
            }
          }
          else if(!strncmp($line, "V D", 3))
          {
            $arcFound = TRUE;
            $n = sscanf($line, "V D=%c", $direction);
          }
          else if(!strncmp($line, "V W", 3))
          {
            $n = sscanf($line, "V W=%f", $airwayWidth);
          }
          else if(!strncmp($line, "DC", 2))
          {
            $arcFound = TRUE;
            $n = sscanf($line, "DC%f", $radius);

            if($n == 1)
            {
                             $arcres = $this->arcRes($radius);
              for ($brg = 0.0; $brg < (2.0*M_PI); $brg += $arcres)
              {
                $point = $koCenter->pointInDirectionAndDistance($brg, $radius);
                $currentPath->appendPoint($point);
                $np++;
              }
            }
          }
          else if(!strncmp($line, "DB", 2))
          {
            $n = sscanf($line, "DB %f:%f:%f %c %f:%f:%f %c,%f:%f:%f %c %f:%f:%f %c",
                   $latdeg, $latmin, $latsec, $latvz,
                   $londeg, $lonmin, $lonsec, $lonvz,
                   $latdeg2, $latmin2, $latsec2, $latvz2,
                   $londeg2, $lonmin2, $lonsec2, $lonvz2
                 );
            $arcFound = TRUE;

            if($n == 16)
            {
              $point1 = new AipPoint();
              $point2 = new AipPoint();

              $point1->lat = $latdeg + $latmin/60.0 + $latsec/3600.0;
              if($latvz == 'S')
              {
                $point1->lat *= -1;
              }
              $point1->lon = $londeg + $lonmin/60.0 + $lonsec/3600.0;
              if($lonvz == 'W')
              {
                $point1->lon *= -1;
              }
              $point2->lat = $latdeg2 + $latmin2/60.0 + $latsec2/3600.0;
              if($latvz2 == 'S')
              {
                $point2->lat *= -1;
              }
              $point2->lon = $londeg2 + $lonmin2/60.0 + $lonsec2/3600.0;
              if($lonvz2 == 'W')
              {
                $point2->lon *= -1;
              }
            }
            else
            {
              $n = sscanf($line, "DB %f:%f %c %f:%f %c,%f:%f %c %f:%f %c",
                                   $latdeg, $latmin, $latvz,
                                   $londeg, $lonmin, $lonvz,
                                   $latdeg2, $latmin2, $latvz2,
                                   $londeg2, $lonmin2, $lonvz2
                                 );

              if($n == 12)
              {
                $point1 = new AipPoint();
                $point2 = new AipPoint();

                $point1->lat = $latdeg + $latmin/60.0;
                if($latvz == 'S')
                {
                  $point1->lat *= -1;
                }
                $point1->lon = $londeg + $lonmin/60.0;
                if($lonvz == 'W')
                {
                  $point1->lon *= -1;
                }
                $point2->lat = $latdeg2 + $latmin2/60.0;
                if($latvz2 == 'S')
                {
                  $point2->lat *= -1;
                }
                $point2->lon = $londeg2 + $lonmin2/60.0;
                if($lonvz2 == 'W')
                {
                  $point2->lon *= -1;
                }
              }
              else
              {
                $n = sscanf($line, "DB %2f%2f%f%c %3f%2f%f%c,%2f%2f%f%c %3f%2f%f%c",
                                   $latdeg, $latmin, $latsec, $latvz,
                                   $londeg, $lonmin, $lonsec, $lonvz,
                                   $latdeg2, $latmin2, $latsec2, $latvz2,
                                   $londeg2, $lonmin2, $lonsec2, $lonvz2
                                   );

                if($n == 16)
                {
                  $point1 = new AipPoint();
                  $point2 = new AipPoint();

                  $point1->lat = $latdeg + $latmin/60.0 + $latsec/3600.0;
                  if($latvz == 'S')
                  {
                    $point1->lat *= -1;
                  }
                  $point1->lon = $londeg + $lonmin/60.0 + $lonsec/3600.0;
                  if($lonvz == 'W')
                  {
                    $point1->lon *= -1;
                  }
                  $point2->lat = $latdeg2 + $latmin2/60.0 + $latsec2/3600.0;
                  if($latvz2 == 'S')
                  {
                    $point2->lat *= -1;
                  }
                  $point2->lon = $londeg2 + $lonmin2/60.0 + $lonsec2/3600.0;
                  if($lonvz2 == 'W')
                  {
                    $point2->lon *= -1;
                  }
                }
                else
                {
                  $n = 0;
                }
              }
            }

            if ($n > 0)
            {
              // compute the points on the arc and add them to the poly and bbox
              $startAngle = $koCenter->bearingTo($point1);
              $endAngle = $koCenter->bearingTo($point2);
              $radius = $koCenter->distanceTo($point1);
              $r2 = $koCenter->distanceTo($point2);
              $radius = max($radius, $r2);

              if ($direction == '-') // CCW
              {
                if ($startAngle <= $endAngle)
                {
                  $endAngle -= 2.0*M_PI;
                }
                $currentPath->appendPoint($point1);
                $np++;
                $arcres = $this->arcRes($radius);
                for ($brg = $startAngle - $arcres; $brg > $endAngle; $brg -= $arcres)
                {
                  $point = $koCenter->pointInDirectionAndDistance($brg, $radius);
                  $currentPath->appendPoint($point);
                  $np++;
                }
                $currentPath->appendPoint($point2);
                $np++;
              }
              else
              {
                if ($startAngle >= $endAngle)
                {
                   $startAngle -= 2.0*M_PI;
                }
                $currentPath->appendPoint($point1);
                  $np++;
                                 $arcres = $this->arcRes($radius);
                for ($brg = $startAngle + $arcres; $brg < $endAngle; $brg += $arcres)
                {
                  $point = $koCenter->pointInDirectionAndDistance($brg, $radius);
                  $currentPath->appendPoint($point);
                  $np++;
                }
                $currentPath->appendPoint($point2);
                $np++;
              }
            }
          }
          else if (!strncmp($line, "DA", 2))
          {
            $arcFound = TRUE;
            $startAngle;
            $endAngle;
            $n = sscanf($line, "DA %f,%f,%f",
              $radius, $startAngle, $endAngle);
            $startAngle = $startAngle * M_PI / 180.0;
            $endAngle = $endAngle * M_PI / 180.0;
            if ($n == 3)
            {
              if ($direction == '-') // CCW
              {
                if ($startAngle < $endAngle)
                {
                  $endAngle -= 2.0*M_PI;
                }
                $arcres = $this->arcRes($radius);
                for ($brg = $startAngle; $brg > $endAngle; $brg -= $arcres)
                {
                  $point = $koCenter->pointInDirectionAndDistance($brg, $radius);
                  $currentPath->appendPoint($point);
                  $np++;
                }
                $point = $koCenter->pointInDirectionAndDistance($endAngle, $radius);
                $currentPath->appendPoint($point);
                $np++;
              }
              else
              {
                if ($startAngle > $endAngle)
                {
                    $startAngle -= 2.0*M_PI;
                }
                $arcres = $this->arcRes($radius);
                for ($brg = $startAngle; $brg < $endAngle; $brg += $arcres)
                {
                  $point = $koCenter->pointInDirectionAndDistance($brg, $radius);
                  $currentPath->appendPoint($point);
                  $np++;
                }
                $point = $koCenter->pointInDirectionAndDistance($endAngle, $radius);
                $currentPath->appendPoint($point);
                $np++;
              }
            }
          } // end DA Element
        } // end started
      } // end while
      // last asp wasn't added
      if ($np > 0)
      {
        $this->updateAspPath($asp, $currentPath, $pathIsAirway, $airwayWidth, $np);
        // add current airspace to array
        $this->airspaces[] = $asp;

        if (($np < 5) && $arcFound)
        {
          $this->warnings .= "Warning: ".$asp->name." contains only ".$np." points<BR>\n";
        }
      }
      return TRUE;
    } // end OPENAIR
    else
    {
      $this->errors .= "ERROR: Unknown source format ".$this->srcFormat."!<BR>\n";
      return FALSE;
    }

    return FALSE;
  }

  public function writeToFile($destPath, $outputFormat, $version)
  {
    //return $this->startConversion($destPath, "OPENAIP", $version);
    // write result to file
    $outHandle = fopen($destPath, 'w');
    if ($outHandle)
    {
      if (!strncmp($outputFormat, "OPENAIR", 7))
      {
        date_default_timezone_set('UTC');
        fwrite($outHandle, utf8_encode("*  **********************************************************************************************\n"));
        fwrite($outHandle, utf8_encode("*  Data converted on " . date('Y-m-d H:i:s') . " UTC\n"));
        fwrite($outHandle, utf8_encode("*  \n"));
        fwrite($outHandle, utf8_encode("*  This data is owned by Butterfly Avionics GmbH and licensed under the CC BY-NC-SA,\n"));
        fwrite($outHandle, utf8_encode("*  not to be used for commercial purposes. For more information on commercial licensing visit\n"));
        fwrite($outHandle, utf8_encode("*  \n"));
        fwrite($outHandle, utf8_encode("*  http://www.openaip.net/commercial-licensing\n"));
        fwrite($outHandle, utf8_encode("*  \n"));
        fwrite($outHandle, utf8_encode("*  openAIP data is not certified and must not be used for primary navigation or flight planning.\n"));
        fwrite($outHandle, utf8_encode("*  NEVER RELY ON OPENAIP DATA. openAIP data contains errors. Using openAIP data may\n"));
        fwrite($outHandle, utf8_encode("*  result in serious injury or death, use at your own risk!\n"));
        fwrite($outHandle, utf8_encode("*  \n"));
        fwrite($outHandle, utf8_encode("*  OPENAIP OFFERS THE WORK AS-IS AND MAKES NO REPRESENTATIONS OR WARRANTIES OF ANY KIND\n"));
        fwrite($outHandle, utf8_encode("*  CONCERNING THE WORK, EXPRESS, IMPLIED, STATUTORY OR OTHERWISE, INCLUDING, WITHOUT LIMITATION,\n"));
        fwrite($outHandle, utf8_encode("*  WARRANTIES OF TITLE, MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, NONINFRINGEMENT, OR\n"));
        fwrite($outHandle, utf8_encode("*  THE ABSENCE OF LATENT OR OTHER DEFECTS, ACCURACY, OR THE PRESENCE OF ABSENCE OF ERRORS,\n"));
        fwrite($outHandle, utf8_encode("*  WHETHER OR NOT DISCOVERABLE. SOME JURISDICTIONS DO NOT ALLOW THE EXCLUSION OF IMPLIED\n"));
        fwrite($outHandle, utf8_encode("*  WARRANTIES, SO THIS EXCLUSION MAY NOT APPLY TO YOU.\n"));
        fwrite($outHandle, utf8_encode("*  \n"));
        fwrite($outHandle, utf8_encode("*  EXCEPT TO THE EXTENT REQUIRED BY APPLICABLE LAW, IN NO EVENT WILL OPENAIP BE LIABLE TO YOU ON\n"));
        fwrite($outHandle, utf8_encode("*  ANY LEGAL THEORY FOR ANY SPECIAL, INCIDENTAL, CONSEQUENTIAL, PUNITIVE OR EXEMPLARY DAMAGES\n"));
        fwrite($outHandle, utf8_encode("*  ARISING OUT OF THIS LICENSE OR THE USE OF THE WORK, EVEN IF OPENAIP HAS BEEN ADVISED OF THE\n"));
        fwrite($outHandle, utf8_encode("*  POSSIBILITY OF SUCH DAMAGES.\n"));
        fwrite($outHandle, utf8_encode("*  **********************************************************************************************\n\n"));

   
        foreach ($this->airspaces as $asp)
        {
          fwrite($outHandle, utf8_encode($asp->toOpenAir()));
          fwrite($outHandle, utf8_encode("\n"));
        }

        fclose($outHandle);
        return TRUE;
      }        
      else if (!strncmp($outputFormat, "OPENAIP", 7))
      {
        fwrite($outHandle, utf8_encode("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>\n"));
        fwrite($outHandle, utf8_encode("<OPENAIP DATAFORMAT=\"1\" VERSION=\"".$version."\" xmlns=\"http://www.butterfly-avionics.com\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:schemaLocation=\"http://www.butterfly-avionics.com openaip.xsd\">\n<AIRSPACES>\n"));

        foreach ($this->airspaces as $asp)
        {
          fwrite($outHandle, utf8_encode($asp->toXml("  ")));
        }

        fwrite($outHandle, utf8_encode("</AIRSPACES>\n</OPENAIP>\n"));

        fclose($outHandle);
        return TRUE;
      }
      else if (!strncmp($outputFormat, "GML", 3))
      {
        fwrite($outHandle, utf8_encode("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>\n"));
        fwrite($outHandle, utf8_encode("<OPENAIP:airspaces
             xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"
             xsi:schemaLocation=\"www.openaip.net/ airspaces.xsd\"
             xmlns:OPENAIP=\"http://www.openaip.net/\"
             xmlns:gml=\"http://www.opengis.net/gml\">
          <gml:boundedBy>
            <gml:Box>
              <gml:coord><gml:X>$this->minLon</gml:X><gml:Y>$this->minLat</gml:Y></gml:coord>
              <gml:coord><gml:X>$this->maxLon</gml:X><gml:Y>$this->maxLat</gml:Y></gml:coord>
            </gml:Box>
          </gml:boundedBy>\n"));

        $fid = 0;
        foreach ($this->airspaces as $asp)
        {
          fwrite($outHandle, utf8_encode($asp->toGml("  ", $fid)));
          $fid += 1;
        }

        fwrite($outHandle, utf8_encode("</OPENAIP:airspaces>\n"));

        fclose($outHandle);
        return TRUE;
      }
      else
      {
        $this->errors .= "ERROR: Unknown output format ".$outputFormat."!<BR>\n";
      }
    }
    $this->errors .= "ERROR: Could not write to destination file ".$destPath."!<BR>\n";
    return FALSE;
  }

}