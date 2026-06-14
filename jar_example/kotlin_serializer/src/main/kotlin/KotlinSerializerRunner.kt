import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.boolean
import kotlinx.serialization.json.buildJsonArray
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.put

class KotlinSerializerRunner {
    fun serializeProfile(name: String, age: Int): String {
        val payload = buildJsonObject {
            put("name", name)
            put("age", age)
            put("enabled", true)
        }
        return Json.encodeToString(JsonObject.serializer(), payload)
    }

    fun extractProfileName(json: String): String {
        return Json.parseToJsonElement(json).jsonObject["name"]!!.jsonPrimitive.content
    }

    fun extractProfileAge(json: String): Int {
        return Json.parseToJsonElement(json).jsonObject["age"]!!.jsonPrimitive.int
    }

    fun isProfileEnabled(json: String): Boolean {
        return Json.parseToJsonElement(json).jsonObject["enabled"]!!.jsonPrimitive.boolean
    }

    fun serializeWrappedProfile(name: String, age: Int): String {
        val payload = buildJsonObject {
            put("profile", buildJsonObject {
                put("name", name)
                put("age", age)
            })
            put("tags", buildJsonArray {
                add(JsonPrimitive("primary"))
                add(JsonPrimitive("beta"))
            })
        }
        return Json.encodeToString(JsonObject.serializer(), payload)
    }

    companion object {
        @JvmStatic
        fun main(args: Array<String>) {
            val runner = KotlinSerializerRunner()
            var start = System.nanoTime()
            val json = runner.serializeProfile("alice", 31)
            var end = System.nanoTime()
            println("Serialized payload: $json")
            println("Time: ${(end - start) / 1_000_000.0} ms")

            start = System.nanoTime()
            println("Extracted name: ${runner.extractProfileName(json)}")
            end = System.nanoTime()
            println("Time: ${(end - start) / 1_000_000.0} ms")

            start = System.nanoTime()
            println("Extracted age: ${runner.extractProfileAge(json)}")
            end = System.nanoTime()
            println("Time: ${(end - start) / 1_000_000.0} ms")

            start = System.nanoTime()
            println("Enabled flag: ${runner.isProfileEnabled(json)}")
            end = System.nanoTime()
            println("Time: ${(end - start) / 1_000_000.0} ms")

            start = System.nanoTime()
            println("Wrapped payload: ${runner.serializeWrappedProfile("alice", 31)}")
            end = System.nanoTime()
            println("Time: ${(end - start) / 1_000_000.0} ms")
        }
    }
}
