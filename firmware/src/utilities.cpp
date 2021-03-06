#include <Arduino.h>


unsigned long GetEpochFromISO8601(String time)
{
  int year = time.substring(0, 4).toInt();
  int month = time.substring(5, 7).toInt();
  int day = time.substring(8, 10).toInt();
  int hour = time.substring(11, 13).toInt();
  int min = time.substring(14, 16).toInt();
  int sec = time.substring(17, 19).toInt();
  setTime(hour, min, sec, day, month, year);
  return now();
}

// Compares two ISO8601 formatted date/time strings and returns true if date/times are within N days.
bool AreDateTimesWithinNDays(String time1, String time2, int days)
{
  unsigned long epoch1 = GetEpochFromISO8601(time1);
  unsigned long epoch2 = GetEpochFromISO8601(time2);
  unsigned long daysInMS = 60 * 60 * 24 * days;

  if (abs(epoch1 - epoch2) <= daysInMS)
  {  
    return true;
  }
 
  return false;
}




