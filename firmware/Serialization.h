#pragma once

enum Direction
{
  CCW = 0,
  CW = 1
};

enum class OutputEventType: long int
{
  Button = 0,
  Dial = 1,
  HeartBeat = 2,
  StartUp = 0xEE5F69,
};

enum InputEventType
{
  color = 0,
  brightness = 1,
  HeartBeat = 2,
};