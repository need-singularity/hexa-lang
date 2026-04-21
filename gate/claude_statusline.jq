(.stack // [])
| if length == 0 then ""
  else
    (. [0:3] | map(
      (if   .command == "drill"  then "🔭"
       elif .command == "scan"   then "🧪"
       elif .command == "record" then "📜"
       elif .command == "smash"  then "💥"
       elif .command == "free"   then "🪁"
       elif .command == "todo"   then "☑️"
       elif .command == "go"     then "🚀"
       elif .command == "keep"   then "🔒"
       elif .command == "diff"   then "📊"
       elif .command == "trace"  then "🔗"
       elif .command == "compact" then "📦"
       else "•" end)
      + " " + (.command // "?") + ": " + (.seed // "?")
    ) | join(" · "))
    + (if length > 3 then " · +\(length - 3)" else "" end)
  end
